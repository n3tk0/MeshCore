// Host test: the registers ota_stage.c uses, with the flash behind them (see test_ota_stage.c)
#pragma once
#include <stdint.h>

typedef struct { volatile uint32_t READY, CONFIG, ERASEPAGE; } fake_nvmc_t;
typedef struct { volatile uint32_t RUNSTATUS, RREN, RR[8]; } fake_wdt_t;
typedef struct { volatile uint32_t SCK, CSN, IO0, IO1, IO2, IO3; } fake_qspi_psel_t;
typedef struct { volatile uint32_t SRC, DST, CNT; } fake_qspi_read_t;
typedef struct {
  fake_qspi_psel_t PSEL;
  fake_qspi_read_t READ;
  volatile uint32_t IFCONFIG0, IFCONFIG1, ENABLE, EVENTS_READY, TASKS_ACTIVATE, TASKS_DEACTIVATE,
                    CINSTRCONF, CINSTRDAT0, TASKS_READSTART;
} fake_qspi_t;

fake_nvmc_t* fake_nvmc(void);
fake_qspi_t* fake_qspi(void);
extern fake_wdt_t fake_wdt;

#define NRF_NVMC  (fake_nvmc())
#define NRF_QSPI  (fake_qspi())
#define NRF_WDT   (&fake_wdt)

#define NVMC_CONFIG_WEN_Ren 0
#define NVMC_CONFIG_WEN_Wen 1
#define NVMC_CONFIG_WEN_Een 2
#define WDT_RR_RR_Reload 0x6E524635UL
#define QSPI_CINSTRCONF_LENGTH_Pos 8
#define QSPI_CINSTRCONF_LIO2_Msk (1UL << 12)
#define QSPI_CINSTRCONF_LIO3_Msk (1UL << 13)
#define QSPI_IFCONFIG1_SCKDELAY_Pos 0
#define QSPI_IFCONFIG1_SCKFREQ_Pos 28
#define NRFX_DELAY_US(us) ((void)(us))
