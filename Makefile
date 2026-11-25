SHELL := /bin/bash

IDF_PATH ?= $(shell if [ -n "$$IDF_PATH" ]; then echo "$$IDF_PATH"; elif [ -d "$$HOME/.espressif/v6.1/esp-idf" ]; then echo "$$HOME/.espressif/v6.1/esp-idf"; else echo "$$HOME/esp/esp-idf"; fi)
PORT ?= /dev/ttyACM0
BAUD ?= 460800
IDF := . "$(IDF_PATH)/export.sh" >/dev/null 2>&1 && idf.py

.PHONY: help setup build wasm test lint-ui flash monitor flash-monitor erase clean distclean menuconfig size doctor

help:
	@echo "dalangs_TFT controller"
	@echo ""
	@echo "  make setup         init submodules, fetch components, set target esp32s3"
	@echo "  make build         build firmware with CMake/Ninja"
	@echo "  make wasm          rebuild the .wasm applets"
	@echo "  make test          run host unit tests"
	@echo "  make lint-ui       check UI/pin/wasm boundaries"
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

wasm:
	./scripts/build_wasm.sh

test:
	./test/run.sh

lint-ui:
	./scripts/lint_ui.sh

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
