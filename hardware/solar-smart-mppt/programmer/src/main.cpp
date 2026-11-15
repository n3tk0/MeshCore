// MSPM0 SWD programmer on an ESP32-C3 SuperMini.
// Wires: GPIO4 SWCLK, GPIO5 SWDIO, GPIO6 NRST, GPIO3 3V3M sense, GPIO7 3V3 out (power option), GND.
// Web page on the local Wi-Fi (http://mspm0-prog.local) or, without Wi-Fi settings, on its own
// access point "MSPM0-Programmer" (password mspm0prog, http://192.168.4.1).
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <rom/ets_sys.h>
#include "Swd.h"
#include "Mspm0Flash.h"
#include "Image.h"
#include "images_gen.h"

#define PIN_SWCLK   4
#define PIN_SWDIO   5
#define PIN_NRST    6
#define PIN_VT      3     // 3V3M of the target board (input, pull-down)
#define PIN_PWR     7     // 3V3 for the target in "power from the programmer" mode
#define PIN_LED     8     // blue LED, low = on
#define PIN_BTN     9     // BOOT button, low = pressed

#define AP_SSID     "MSPM0-Programmer"
#define AP_PASS     "mspm0prog"
#define HOSTNAME    "mspm0-prog"
#define PROG_VERSION "0.1"

// ---------------- SWD pins ----------------
class C3Pins : public SwdPins {
public:
  uint32_t half_us = 1;   // ~250-400 kHz with the GPIO overhead: safe on 10-20 cm wires
  void begin() {
    pinMode(PIN_SWCLK, OUTPUT);
    digitalWrite(PIN_SWCLK, LOW);
    pinMode(PIN_SWDIO, OUTPUT);
    digitalWrite(PIN_SWDIO, HIGH);
  }
  void end() {   // all lines released: the programmer may stay connected
    pinMode(PIN_SWCLK, INPUT);
    pinMode(PIN_SWDIO, INPUT);
    pinMode(PIN_NRST, INPUT);
  }
  void clockOut(bool bit) override {
    digitalWrite(PIN_SWDIO, bit);
    digitalWrite(PIN_SWCLK, LOW);
    ets_delay_us(half_us);
    digitalWrite(PIN_SWCLK, HIGH);
    ets_delay_us(half_us);
  }
  bool clockIn() override {
    digitalWrite(PIN_SWCLK, LOW);
    ets_delay_us(half_us);
    bool b = digitalRead(PIN_SWDIO);
    digitalWrite(PIN_SWCLK, HIGH);
    ets_delay_us(half_us);
    return b;
  }
  void release() override { pinMode(PIN_SWDIO, INPUT_PULLUP); }
  void drive() override { pinMode(PIN_SWDIO, OUTPUT); }
  void reset(bool low) override {   // the board has its own pull-up on NRST: never drive it high
    if (low) { pinMode(PIN_NRST, OUTPUT); digitalWrite(PIN_NRST, LOW); }
    else pinMode(PIN_NRST, INPUT);
  }
  void delayMs(uint32_t ms) override { delay(ms); }
};

static C3Pins pins;
static Swd swd(pins);
static Mspm0Flash flasher(swd, pins);
static WebServer server(80);
static Preferences prefs;

// ---------------- state ----------------
enum { MODE_BOARD = 0, MODE_PROG = 1 };
static uint8_t power_mode = MODE_BOARD;
static uint8_t selected = 0;               // 0..N-1 built in, N = uploaded
static const int NBUILTIN = sizeof(builtin) / sizeof(builtin[0]);
static uint8_t* upload_img = nullptr;      // IMAGE_MAX bytes
static uint32_t upload_len = 0;
static char upload_name[48] = "";
static char* upload_text = nullptr;        // .hex text while it comes in (allocated per upload)
static size_t upload_text_len = 0, upload_text_cap = 0;
#define UPLOAD_TEXT_MAX (IMAGE_MAX * 3)    // a .hex is ~2.8x the binary

static volatile bool busy = false, start_req = false;
static volatile uint32_t prog_done = 0, prog_total = 0;
static char prog_step[16] = "";
static char result[160] = "";
static bool last_ok = false;
static uint32_t result_at = 0;
static bool ap_mode = false;

static const uint8_t* imgData(int i, uint32_t* len) {
  if (i < NBUILTIN) { *len = builtin[i].len; return builtin[i].data; }
  *len = upload_len;
  return upload_len ? upload_img : nullptr;
}

static bool targetPowered() { return digitalRead(PIN_VT); }

static void progress(const char* step, uint32_t done, uint32_t total) {
  strncpy(prog_step, step, sizeof(prog_step) - 1);
  prog_done = done;
  prog_total = total;
}

static void setResult(bool ok, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(result, sizeof(result), fmt, ap);
  va_end(ap);
  last_ok = ok;
  result_at = millis();
  Serial.println(result);
}

// ---------------- flashing (own task, so the page keeps answering) ----------------
static void flashJob() {
  uint32_t len;
  const uint8_t* img = imgData(selected, &len);
  if (!img || !imageLooksValid(img, len)) { setResult(false, "Няма валиден образ"); return; }

  bool powered_by_us = false;
  if (power_mode == MODE_PROG) {
    if (targetPowered()) {
      setResult(false, "3V3M вече има напрежение (батерия?): изключи я или избери захранване от платката");
      return;
    }
    pinMode(PIN_PWR, OUTPUT);
    digitalWrite(PIN_PWR, HIGH);
    powered_by_us = true;
    delay(100);
  }
  if (!targetPowered()) {
    if (powered_by_us) pinMode(PIN_PWR, INPUT);
    setResult(false, power_mode == MODE_PROG ? "3V3M не се вдига от програматора (късо? консумация?)"
                                             : "Няма 3V3M: платката няма захранване");
    return;
  }

  pins.begin();
  Mspm0Info info;
  int r = flasher.attach(&info);
  if (r == SWD_OK && info.flash_kb && len > (uint32_t)info.flash_kb * 1024) r = SWD_FLASH;
  if (r == SWD_OK) r = flasher.write(0, img, len, progress);
  if (r == SWD_OK) r = flasher.run(true);
  pins.end();
  if (powered_by_us) { delay(300); pinMode(PIN_PWR, INPUT); }   // let it save its first state

  if (r == SWD_OK) {
    setResult(true, "Записан: %s, %u байта (DPIDR %08X, DEVICEID %08X, flash %u KB)",
              selected < NBUILTIN ? builtin[selected].key : upload_name, (unsigned)len,
              (unsigned)info.dpidr, (unsigned)info.deviceid, info.flash_kb);
  } else {
    setResult(false, "Грешка: %s (адрес 0x%05X, DPIDR %08X)", swdError(r),
              (unsigned)flasher.fail_addr, (unsigned)swd.idcode);
  }
}

static void flashTask(void*) {
  for (;;) {
    if (start_req) {
      start_req = false;
      busy = true;
      progress("start", 0, 0);
      flashJob();
      progress("", 0, 0);
      busy = false;
    }
    delay(20);
  }
}

// ---------------- web page ----------------
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="bg"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>MSPM0 програматор</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;background:#f6f7f8;color:#1c1f22}
h1{font-size:20px}h2{font-size:15px;margin:20px 0 8px}
.card{background:#fff;border:1px solid #dde1e5;border-radius:8px;padding:12px 14px;margin:10px 0}
label{display:block;margin:6px 0}button{font-size:15px;padding:8px 14px;border-radius:6px;border:1px solid #888;background:#fff}
button.main{background:#1f7a4d;color:#fff;border-color:#1f7a4d;width:100%;padding:12px;font-size:17px}
button:disabled{opacity:.5}.ok{color:#1f7a4d}.err{color:#b3261e}.muted{color:#666;font-size:13px}
progress{width:100%}input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:6px}
@media(prefers-color-scheme:dark){body{background:#16181a;color:#e6e8ea}.card{background:#1f2225;border-color:#33373b}
button{background:#2a2e32;color:#e6e8ea}.muted{color:#9aa0a6}}
</style></head><body>
<h1>MSPM0 програматор</h1>
<div class="card"><div id="vt">…</div><div class="muted" id="net"></div></div>
<h2>Образ</h2><div class="card" id="imgs"></div>
<div class="card"><label>Друг образ (.hex или .bin): <input type="file" id="file" accept=".hex,.bin"></label>
<button onclick="up('/upload','file')">Качи</button> <span class="muted" id="upmsg"></span></div>
<h2>Захранване на MSPM0</h2><div class="card">
<label><input type="radio" name="pm" value="0" onchange="setMode(0)"> От платката (батерия или панел), програматорът само проверява 3V3M</label>
<label><input type="radio" name="pm" value="1" onchange="setMode(1)"> От програматора (GPIO7 към 3V3M, само без батерия)</label></div>
<button class="main" id="go" onclick="go()">Запиши</button>
<div class="card"><progress id="pr" max="1" value="0"></progress><div id="res"></div></div>
<h2>Wi-Fi</h2><div class="card">
<label>Мрежа <input type="text" id="ssid"></label><label>Парола <input type="password" id="pass"></label>
<button onclick="wifi()">Запази и рестартирай</button></div>
<h2>Update на програматора</h2><div class="card">
<label>firmware.bin <input type="file" id="ota" accept=".bin"></label>
<button onclick="up('/update','ota')">Обнови</button> <span class="muted" id="otamsg"></span>
<div class="muted" id="ver"></div></div>
<script>
let st={};
async function post(u,b){const r=await fetch(u,{method:'POST',body:b});return r.text()}
async function setMode(m){await post('/mode?m='+m);poll()}
async function sel(i){await post('/select?i='+i);poll()}
async function go(){document.getElementById('res').textContent='';await post('/flash');poll()}
async function wifi(){const f=new FormData();f.append('ssid',ssid.value);f.append('pass',pass.value);
 document.getElementById('net').textContent=await post('/wifi',f)}
async function up(u,id){const el=document.getElementById(id);if(!el.files.length)return;
 const m=document.getElementById(id=='ota'?'otamsg':'upmsg');m.textContent='качва се…';
 const f=new FormData();f.append('f',el.files[0],el.files[0].name);
 try{m.textContent=await post(u,f)}catch(e){m.textContent='връзката прекъсна (рестарт?)'}poll()}
async function poll(){try{st=await (await fetch('/status')).json()}catch(e){return}
 vt.innerHTML=st.vt?'<b class="ok">3V3M: има напрежение</b>':'<b class="err">3V3M: няма напрежение</b>';
 net.textContent=st.net;ver.textContent='Версия '+st.ver+', образи от '+st.rev;
 let h='';st.imgs.forEach((im,i)=>{h+='<label><input type="radio" name="im" '+(i==st.sel?'checked':'')+
  (im.len?'':' disabled')+' onchange="sel('+i+')"> '+im.name+' <span class="muted">'+(im.len?im.len+' байта':'няма')+'</span></label>'});
 imgs.innerHTML=h;document.querySelectorAll('input[name=pm]').forEach(r=>r.checked=(r.value==st.mode));
 go.disabled=st.busy;pr.max=st.total||1;pr.value=st.busy?st.done:0;
 res.innerHTML=st.busy?('<span class="muted">'+st.step+'…</span>'):(st.res?('<span class="'+(st.ok?'ok':'err')+'">'+st.res+'</span>'):'');
 setTimeout(poll,st.busy?300:1500)}
poll();
</script></body></html>)HTML";

static String jsonEsc(const char* s) {
  String o;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') { o += '\\'; o += *s; }
    else if ((uint8_t)*s < 0x20) o += ' ';
    else o += *s;
  }
  return o;
}

static void handleStatus() {
  String j = "{\"vt\":";
  j += targetPowered() ? "true" : "false";
  j += ",\"mode\":" + String(power_mode) + ",\"sel\":" + String(selected);
  j += ",\"busy\":" + String(busy ? "true" : "false");
  j += ",\"step\":\"" + jsonEsc(prog_step) + "\",\"done\":" + String(prog_done) + ",\"total\":" + String(prog_total);
  j += ",\"res\":\"" + jsonEsc(result) + "\",\"ok\":" + String(last_ok ? "true" : "false");
  j += ",\"ver\":\"" PROG_VERSION "\",\"rev\":\"" IMAGES_REV "\",\"net\":\"";
  if (ap_mode) j += "Точка за достъп " AP_SSID ", http://192.168.4.1";
  else j += "Wi-Fi " + jsonEsc(WiFi.SSID().c_str()) + ", http://" HOSTNAME ".local (" + WiFi.localIP().toString() + ")";
  j += "\",\"imgs\":[";
  for (int i = 0; i < NBUILTIN; i++) {
    j += "{\"name\":\"" + jsonEsc(builtin[i].name) + "\",\"len\":" + String(builtin[i].len) + "},";
  }
  String up = upload_len ? String("Качен: ") + upload_name : String("Качен образ");
  j += "{\"name\":\"" + jsonEsc(up.c_str()) + "\",\"len\":" + String(upload_len) + "}]}";
  server.send(200, "application/json", j);
}

static void handleUploadData() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    upload_text_len = 0;
    free(upload_text);
    upload_text_cap = UPLOAD_TEXT_MAX;
    upload_text = (char*)malloc(upload_text_cap);
    if (!upload_text) { upload_text_cap = 96 * 1024; upload_text = (char*)malloc(upload_text_cap); }
    if (!upload_text) upload_text_cap = 0;
    strncpy(upload_name, u.filename.c_str(), sizeof(upload_name) - 1);
    upload_name[sizeof(upload_name) - 1] = 0;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upload_text_len + u.currentSize <= upload_text_cap) {
      memcpy(upload_text + upload_text_len, u.buf, u.currentSize);
    }
    upload_text_len += u.currentSize;   // past the limit: refused below
  }
}

static void parseUpload();
static void handleUploadDone() {
  parseUpload();
  free(upload_text);
  upload_text = nullptr;
  upload_text_cap = 0;
}

static void parseUpload() {
  if (busy) { server.send(409, "text/plain", "Записът тече"); return; }
  if (!upload_img || !upload_text) { server.send(500, "text/plain", "Няма памет"); return; }
  if (upload_text_len == 0 || upload_text_len > upload_text_cap) {
    server.send(400, "text/plain", "Файлът е празен или твърде голям");
    return;
  }
  int n;
  if (upload_text[0] == ':') {
    n = hexToBin(upload_text, upload_text_len, upload_img, IMAGE_MAX);
    if (n < 0) {
      upload_len = 0;
      server.send(400, "text/plain", n == -2 ? "HEX: грешна контролна сума" : n == -3 ? "HEX: адрес извън 0..0xEFFF" : "HEX: невалиден файл");
      return;
    }
  } else {
    if (upload_text_len > IMAGE_MAX) { upload_len = 0; server.send(400, "text/plain", "BIN: над 60 KB"); return; }
    memset(upload_img, 0xFF, IMAGE_MAX);
    memcpy(upload_img, upload_text, upload_text_len);
    n = (int)upload_text_len;
  }
  if (!imageLooksValid(upload_img, n)) {
    upload_len = 0;
    server.send(400, "text/plain", "Не прилича на firmware за MSPM0 (векторна таблица)");
    return;
  }
  upload_len = n;
  selected = NBUILTIN;
  server.send(200, "text/plain", String("Качен, ") + n + " байта, избран");
}

static bool ota_ok = false;
static void handleOtaData() {
  HTTPUpload& u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    ota_ok = !busy && Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (ota_ok && Update.write(u.buf, u.currentSize) != u.currentSize) ota_ok = false;
  } else if (u.status == UPLOAD_FILE_END) {
    if (ota_ok) ota_ok = Update.end(true);
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    ota_ok = false;
  }
}

static void handleOtaDone() {
  if (!ota_ok) { server.send(400, "text/plain", String("Неуспешно: ") + Update.errorString()); return; }
  server.send(200, "text/plain", "Обновен, рестартира се");
  delay(300);
  ESP.restart();
}

static void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/select", HTTP_POST, [] {
    int i = server.arg("i").toInt();
    uint32_t len;
    if (busy || i < 0 || i > NBUILTIN || !imgData(i, &len)) { server.send(400, "text/plain", "no"); return; }
    selected = i;
    if (i < NBUILTIN) prefs.putUChar("sel", i);
    server.send(200, "text/plain", "ok");
  });
  server.on("/mode", HTTP_POST, [] {
    int m = server.arg("m").toInt();
    if (busy || (m != MODE_BOARD && m != MODE_PROG)) { server.send(400, "text/plain", "no"); return; }
    power_mode = m;
    prefs.putUChar("pmode", m);
    server.send(200, "text/plain", "ok");
  });
  server.on("/flash", HTTP_POST, [] {
    if (busy) { server.send(409, "text/plain", "busy"); return; }
    result[0] = 0;
    start_req = true;
    server.send(200, "text/plain", "ok");
  });
  server.on("/wifi", HTTP_POST, [] {
    String s = server.arg("ssid"), p = server.arg("pass");
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    server.send(200, "text/plain", s.length() ? "Запазено, свързва се с " + s + " (http://" HOSTNAME ".local)"
                                               : "Изтрито, остава точка за достъп");
    delay(500);
    ESP.restart();
  });
  server.on("/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/update", HTTP_POST, handleOtaDone, handleOtaData);
  server.begin();
}

static void setupWifi() {
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  WiFi.setHostname(HOSTNAME);
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) {
      digitalWrite(PIN_LED, (i / 2) & 1);
      delay(100);
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    ap_mode = false;
    Serial.printf("Wi-Fi %s, http://%s.local (%s)\n", ssid.c_str(), HOSTNAME, WiFi.localIP().toString().c_str());
  } else {
    ap_mode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("access point %s / %s, http://192.168.4.1\n", AP_SSID, AP_PASS);
  }
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
}

// ---------------- LED and button ----------------
static void led() {
  uint32_t t = millis();
  bool on;
  if (busy) on = (t / 150) & 1;
  else if (result[0] && t - result_at < 5000) on = last_ok ? true : ((t / 80) & 1);
  else on = ap_mode ? ((t % 2000) < 60) : false;   // short flash every 2 s = no Wi-Fi, own access point
  digitalWrite(PIN_LED, on ? LOW : HIGH);
}

static void button() {
  static uint32_t down_at = 0;
  bool down = !digitalRead(PIN_BTN);
  if (down && !down_at) down_at = millis() | 1;
  if (!down && down_at) {
    uint32_t held = millis() - down_at;
    down_at = 0;
    if (held >= 50 && held < 3000 && !busy) { result[0] = 0; start_req = true; }
  }
  // held 10 s: forget the Wi-Fi settings (back to the access point)
  if (down && down_at && millis() - down_at > 10000) {
    prefs.remove("ssid");
    prefs.remove("pass");
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_VT, INPUT_PULLDOWN);
  pinMode(PIN_PWR, INPUT);   // never LOW: that would short a powered board's 3V3M
  pins.end();

  upload_img = (uint8_t*)malloc(IMAGE_MAX);   // null: uploads are refused, the built-in images still work
  prefs.begin("mspm0prog", false);
  power_mode = prefs.getUChar("pmode", MODE_BOARD) == MODE_PROG ? MODE_PROG : MODE_BOARD;
  selected = prefs.getUChar("sel", 0);
  if (selected >= NBUILTIN) selected = 0;

  setupWifi();
  setupWeb();
  xTaskCreate(flashTask, "flash", 6144, nullptr, 1, nullptr);
}

void loop() {
  server.handleClient();
  button();
  led();
  delay(2);
}
