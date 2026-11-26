SHELL := /bin/bash

IDF_PATH ?= $(shell if [ -n "$$IDF_PATH" ]; then echo "$$IDF_PATH"; elif [ -d "$$HOME/.espressif/v6.1/esp-idf" ]; then echo "$$HOME/.espressif/v6.1/esp-idf"; else echo "$$HOME/esp/esp-idf"; fi)
PORT ?= /dev/ttyACM0
BAUD ?= 460800
IDF := . "$(IDF_PATH)/export.sh" >/dev/null 2>&1 && idf.py

.PHONY: help setup build wasm test format lint-ui lcd-ili9341 lcd-st7789 lcd-mode0 lcd-mode3 lcd-selftest lcd-selftest-off camera-on camera-off camera-colorbar camera-colorbar-off audit audit-off touch-debug touch-debug-off wasm-smoke wasm-smoke-off flash monitor flash-monitor erase clean distclean menuconfig size doctor

help:
	@echo "dalangs_TFT controller"
	@echo ""
	@echo "  make setup         init submodules, fetch components, set target esp32s3"
	@echo "  make build         build firmware with CMake/Ninja"
	@echo "  make wasm          rebuild the .wasm applets"
	@echo "  make test          run host unit tests"
	@echo "  make format        apply clang-format (LLVM) to the C sources"
	@echo "  make lint-ui       check UI/pin/wasm boundaries"
	@echo "  make lcd-ili9341   switch sdkconfig to the ILI9341 driver"
	@echo "  make lcd-st7789    switch sdkconfig to the ST7789 driver"
	@echo "  make lcd-mode0     LCD SPI mode 0 (default)"
	@echo "  make lcd-mode3     LCD SPI mode 3"
	@echo "  make lcd-selftest  loop full-screen colors at boot"
	@echo "  make camera-off    test UI without camera preview"
	@echo "  make audit         log heap/stack/pin audit at boot"
	@echo "  make camera-on     restore camera preview"
	@echo "  make camera-colorbar test the sensor color bars"
	@echo "  make touch-debug   log raw/mapped touch coordinates"
	@echo "  make wasm-smoke    load all wasm applets once at boot"
	@echo "  make flash         flash over PORT=$(PORT)"
	@echo "  make monitor       open the serial monitor"
	@echo "  make flash-monitor flash and monitor"
	@echo "  make erase         erase the whole flash"
	@echo "  make clean         idf.py fullclean"
	@echo "  make distclean     remove build, managed components, sdkconfig"
	@echo "  make menuconfig    open the ESP-IDF config menu"
	@echo "  make size          show component sizes (after build)"
	@echo "  make doctor        print tool versions"
	@echo ""
	@echo "Override: make flash PORT=/dev/ttyUSB0 IDF_PATH=/path/to/esp-idf"

setup:
	git submodule update --init --recursive
	@test -d "$(IDF_PATH)" || { echo "ESP-IDF not found at $(IDF_PATH). Set IDF_PATH."; exit 1; }
	@if [ ! -f sdkconfig ]; then \
		$(IDF) set-target esp32s3; \
	else \
		$(IDF) reconfigure; \
	fi

build:
	@test -d "$(IDF_PATH)" || { echo "ESP-IDF not found at $(IDF_PATH). Set IDF_PATH."; exit 1; }
	$(IDF) build

format:
	@clang-format -i main/main.c main/src/*.c main/include/*.h main/wasm/*.c test/*.c
	@echo "clang-format applied (generated fonts are skipped on purpose)"

wasm:
	./scripts/build_wasm.sh

test:
	./test/run.sh

lint-ui:
	./scripts/lint_ui.sh

lcd-ili9341:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_LCD_ST7789=y/# CONFIG_DALANGS_LCD_ST7789 is not set/; s/^# CONFIG_DALANGS_LCD_ILI9341 is not set/CONFIG_DALANGS_LCD_ILI9341=y/' sdkconfig
	@echo "LCD controller: ILI9341. Run: make build flash monitor"

lcd-st7789:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^# CONFIG_DALANGS_LCD_ST7789 is not set/CONFIG_DALANGS_LCD_ST7789=y/; s/^CONFIG_DALANGS_LCD_ILI9341=y/# CONFIG_DALANGS_LCD_ILI9341 is not set/' sdkconfig
	@echo "LCD controller: ST7789. Run: make build flash monitor"

lcd-mode0:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_LCD_SPI_MODE_3=y/# CONFIG_DALANGS_LCD_SPI_MODE_3 is not set/; s/^# CONFIG_DALANGS_LCD_SPI_MODE_0 is not set/CONFIG_DALANGS_LCD_SPI_MODE_0=y/' sdkconfig
	@echo "LCD SPI mode 0. Run: make build flash monitor"

lcd-mode3:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_LCD_SPI_MODE_0=y/# CONFIG_DALANGS_LCD_SPI_MODE_0 is not set/; s/^# CONFIG_DALANGS_LCD_SPI_MODE_3 is not set/CONFIG_DALANGS_LCD_SPI_MODE_3=y/' sdkconfig
	@echo "LCD SPI mode 3. Run: make build flash monitor"

camera-off:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_CAMERA_ENABLE=y/# CONFIG_DALANGS_CAMERA_ENABLE is not set/' sdkconfig
	@echo "Camera preview disabled. Run: make build flash monitor"

camera-on:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^# CONFIG_DALANGS_CAMERA_ENABLE is not set/CONFIG_DALANGS_CAMERA_ENABLE=y/' sdkconfig
	@echo "Camera preview enabled. Run: make build flash monitor"

audit:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^# CONFIG_DALANGS_FIRMWARE_AUDIT is not set/CONFIG_DALANGS_FIRMWARE_AUDIT=y/' sdkconfig
	@echo "Firmware audit enabled. Run: make build flash monitor"

audit-off:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_FIRMWARE_AUDIT=y/# CONFIG_DALANGS_FIRMWARE_AUDIT is not set/' sdkconfig
	@echo "Firmware audit disabled."

camera-colorbar:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^# CONFIG_DALANGS_CAMERA_COLORBAR is not set/CONFIG_DALANGS_CAMERA_COLORBAR=y/' sdkconfig
	@echo "Camera color bar enabled. Run: make camera-on build flash monitor"

camera-colorbar-off:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_CAMERA_COLORBAR=y/# CONFIG_DALANGS_CAMERA_COLORBAR is not set/' sdkconfig
	@echo "Camera color bar disabled."

touch-debug:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^# CONFIG_DALANGS_TOUCH_DEBUG is not set/CONFIG_DALANGS_TOUCH_DEBUG=y/' sdkconfig
	@echo "Touch debug enabled. Run: make build flash monitor"

touch-debug-off:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_TOUCH_DEBUG=y/# CONFIG_DALANGS_TOUCH_DEBUG is not set/' sdkconfig
	@echo "Touch debug disabled."

wasm-smoke:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^# CONFIG_DALANGS_WASM_SMOKE_TEST is not set/CONFIG_DALANGS_WASM_SMOKE_TEST=y/' sdkconfig
	@echo "Wasm smoke test enabled. Run: make build flash monitor"

wasm-smoke-off:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_WASM_SMOKE_TEST=y/# CONFIG_DALANGS_WASM_SMOKE_TEST is not set/' sdkconfig
	@echo "Wasm smoke test disabled."

lcd-selftest:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^# CONFIG_DALANGS_LCD_SELF_TEST is not set/CONFIG_DALANGS_LCD_SELF_TEST=y/' sdkconfig
	@echo "LCD self test enabled. Run: make build flash monitor"

lcd-selftest-off:
	@test -f sdkconfig || { echo "Run make setup first."; exit 1; }
	@sed -i 's/^CONFIG_DALANGS_LCD_SELF_TEST=y/# CONFIG_DALANGS_LCD_SELF_TEST is not set/' sdkconfig
	@echo "LCD self test disabled."

flash:
	$(IDF) -p "$(PORT)" -b "$(BAUD)" flash

monitor:
	$(IDF) -p "$(PORT)" monitor

flash-monitor:
	$(IDF) -p "$(PORT)" -b "$(BAUD)" flash monitor

erase:
	$(IDF) -p "$(PORT)" erase-flash

clean:
	$(IDF) fullclean

distclean:
	rm -rf build managed_components sdkconfig sdkconfig.old dependencies.lock

menuconfig:
	$(IDF) menuconfig

size:
	$(IDF) size-components

doctor:
	@echo "IDF_PATH=$(IDF_PATH)"
	@cmake --version | head -1
	@ninja --version
	@clang --version | head -1 || true
	@command -v wasm-ld || echo "wasm-ld: not on PATH (will use esp-clang copy)"
