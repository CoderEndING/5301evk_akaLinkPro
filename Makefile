# akaLinkPro build & flash shortcuts (Windows)
#
# Targets (BOARD=hpm5301evklite):
#   make build      - build bootloader + APP
#   make build-boot - build DFU bootloader only  (build_xip_evklite)
#   make build-app  - build APP only             (build_dfu_evklite)
#   make flash      - J-Link first flash: bootloader + APP in one session
#   make flash-app  - J-Link flash APP only (bootloader preserved)
#   make dfu        - dfu-util download of the APP (needs dfu-util in PATH)
#   make reset-usb  - force a USB re-enumeration of the probe
#   make clean      - remove the evklite build directories
#
# Hardware test targets (ST/EVKLite target board must be wired up):
#   make sram-test  - STM32F103 SRAM read/write benchmark over CMSIS-DAP+OpenOCD
#   make rtt-test   - STM32F103 SEGGER RTT throughput test (probe -> host)
#   make uart-echo  - EVKLite CDC loopback check   (short J3.8 <-> J3.10 first)
#   make uart-loop  - EVKLite CDC loopback sweep, 9600 .. 10 Mbps
#
# Variables:
#   PYTHON  - interpreter for the test scripts (needs pyserial for uart-*)
#   COM     - CDC port of the probe, e.g. COM7
#   OPENOCD_EXE / OPENOCD_SCRIPTS / HPM_SDK_ENV_DIR - tool locations
#
# The recipes delegate to the maintained .bat scripts, which carry the
# toolchain paths (E:\sdk_env_v1.11.0, overridable via HPM_SDK_ENV_DIR).
# J-Link install dir can be overridden with JLINK_EXE.

SHELL      := cmd.exe
.SHELLFLAGS := /c

APP_DIR  = firmware\application_5301
BOOT_DIR = firmware\bootloader_dfu

PYTHON ?= python
COM    ?= COM52

.PHONY: all help build build-boot build-app flash flash-app dfu reset-usb clean \
        sram-test rtt-test rtt-max rtt-link uart-echo uart-loop
all: help

build: build-boot build-app

build-boot:
	@echo [make] building bootloader ...
	@cd /d $(BOOT_DIR) && build_xip_evklite.bat

build-app:
	@echo [make] building APP ...
	@cd /d $(APP_DIR) && build_dfu_evklite.bat

flash:
	@echo [make] J-Link first flash: bootloader + APP ...
	@cd /d $(APP_DIR) && flash_jlink_evklite_full.bat

flash-app:
	@echo [make] J-Link flash APP (bootloader preserved) ...
	@cd /d $(APP_DIR) && flash_jlink_evklite.bat

dfu:
	@echo [make] dfu-util download of the APP ...
	@cd /d $(APP_DIR) && program_evklite.bat

reset-usb:
	powershell -NoProfile -ExecutionPolicy Bypass -File script_test\reset_akalink_usb.ps1

# --- hardware tests ---------------------------------------------------------
# STM32F103 SRAM read/write speed: OpenOCD load_image/dump_image of 20 KB at
# 0x20000000 across SWD clocks 1..60 MHz, byte-compared every run.
# The target is normalised to 64 MHz first: an STM32F1 left at its reset
# default (HSI 8 MHz) caps the throughput near 1.4 MB/s whatever the SWD clock
# is. Use `python script_test\sram_speed_test.py --no-boost` to keep it as-is.
sram-test:
	@echo [make] STM32F103 SRAM read/write benchmark (CMSIS-DAP + OpenOCD) ...
	$(PYTHON) script_test\sram_speed_test.py

# SEGGER RTT throughput from the bundled STM32F103 test firmware.
# The target clock is normalised to 64 MHz first: an STM32F1 out of reset runs
# at 8 MHz and its RTT write loop then caps the stream at ~277 KB/s whatever
# the SWD clock is.
rtt-test:
	@echo [make] STM32F103 SEGGER RTT throughput test (OpenOCD rtt server) ...
	$(PYTHON) script_test\rtt_speed_test.py

# Ceiling measurement: the poll loop runs inside OpenOCD (no telnet round
# trips), 32-bit reads in bounded chunks. ~1.1 MB/s at 60 MHz.
rtt-max:
	@echo [make] RTT drain ceiling (poll loop inside OpenOCD) ...
	$(PYTHON) script_test\rtt_drain_bench.py

# SWD read reliability while the target RUNS (AP reads fail at high clocks).
rtt-link:
	@echo [make] SWD link matrix while the target runs ...
	$(OPENOCD_EXE) -s "$(OPENOCD_SCRIPTS)" -f script_test\openocd_stm32f1_swd.cfg \
	  -c "gdb port disabled" -c "tcl port disabled" -c "telnet port disabled" \
	  -c "init" -c "reset run" -c "source script_test/rtt_link_matrix.tcl" -c "shutdown"

# EVKLite CDC loopback (short J3.8 = UART_TXD to J3.10 = UART_RXD first).
uart-echo:
	@echo [make] CDC loopback check on $(COM) ...
	$(PYTHON) script_test\evk_echo.py $(COM) 115200 make-echo

uart-loop:
	@echo [make] CDC loopback sweep on $(COM) ...
	$(PYTHON) script_test\uart_loopback_common.py $(COM)

clean:
	@echo [make] cleaning evklite build dirs ...
	rmdir /s /q $(APP_DIR)\build_dfu_evklite
	rmdir /s /q $(BOOT_DIR)\build_xip_evklite

help:
	@echo akaLinkPro targets:
	@echo   make build       build bootloader + APP
	@echo   make build-boot  build DFU bootloader only
	@echo   make build-app   build APP only
	@echo   make flash       J-Link first flash (bootloader + APP)
	@echo   make flash-app   J-Link flash APP only
	@echo   make dfu         dfu-util download of the APP
	@echo   make reset-usb   force USB re-enumeration of the probe
	@echo   make sram-test   STM32F103 SRAM read/write speed test (OpenOCD)
	@echo   make rtt-test    STM32F103 SEGGER RTT throughput (rtt server)
	@echo   make rtt-max     RTT drain ceiling (poll loop inside OpenOCD)
	@echo   make rtt-link    SWD read reliability while the target runs
	@echo   make uart-echo   CDC loopback check (COM=COMx)
	@echo   make uart-loop   CDC loopback sweep 9600..10M (COM=COMx)
	@echo   make clean       remove evklite build directories
