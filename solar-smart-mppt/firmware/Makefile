# solar-smart-mppt firmware (MSPM0L1306). Needs arm-none-eabi-gcc and the TI MSPM0 SDK:
#   make SDK_ROOT=/path/to/mspm0-sdk        -> build/smppt.hex, build/smppt.bin
#   make test                               -> host unit tests (gcc)
SDK_ROOT ?= $(HOME)/mspm0-sdk
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size
BUILD   = build

SRC = src/main.c src/hal.c src/analog.c src/i2c_target.c src/regs.c src/settings.c src/gauge.c src/bms.c src/bq25798.c
DL  = dl_common.c dl_i2c.c dl_flashctl.c dl_adc12.c dl_comp.c dl_opa.c dl_vref.c dl_timer.c m0p/dl_interrupt.c \
      m0p/sysctl/dl_sysctl_mspm0l11xx_l13xx.c
STARTUP = $(SDK_ROOT)/source/ti/devices/msp/m0p/startup_system_files/gcc/startup_mspm0l130x_gcc.c

CFLAGS = -mcpu=cortex-m0plus -mthumb -Os -g -std=c11 -Wall -Wextra -Wno-unused-parameter \
         -ffunction-sections -fdata-sections -D__MSPM0L1306__ \
         -I src -I $(SDK_ROOT)/source -I $(SDK_ROOT)/source/third_party/CMSIS/Core/Include
LDFLAGS = -mcpu=cortex-m0plus -mthumb -T tools/mspm0l1306_smppt.lds -Wl,--gc-sections -Wl,-Map=$(BUILD)/smppt.map \
          --specs=nano.specs --specs=nosys.specs -nostartfiles

OBJS = $(patsubst src/%.c,$(BUILD)/%.o,$(SRC)) $(patsubst %.c,$(BUILD)/dl/%.o,$(DL)) $(BUILD)/startup.o

all: $(BUILD)/smppt.hex $(BUILD)/smppt.bin

$(BUILD)/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/dl/%.o: $(SDK_ROOT)/source/ti/driverlib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/startup.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(BUILD)/smppt.elf: $(OBJS) tools/mspm0l1306_smppt.lds
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD)/smppt.hex: $(BUILD)/smppt.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/smppt.bin: $(BUILD)/smppt.elf
	$(OBJCOPY) -O binary $< $@

HOST_SRC = test/test_main.c src/regs.c src/settings.c src/gauge.c src/bms.c src/bq25798.c
test: $(BUILD)/host_test
	$(BUILD)/host_test

$(BUILD)/host_test: $(HOST_SRC) src/*.h
	@mkdir -p $(BUILD)
	gcc -std=c11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -g -I src -I test $(HOST_SRC) -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
