# Makefile — CPU1 (C28x) build for projects/launchxl-f280039c-led-chaser
#
# Invoked ONLY through harness/tools/ccs.ps1 (AGENTS.md §1):
#   gmake all CONFIG=CPU1_FLASH CCS_ROOT=<ccs> CGT_ROOT=<cgt> C2000WARE_ROOT=<sdk>
#
# Defaults below are for this machine; the wrapper always overrides them from
# harness/env/manifest.json (doctor.ps1 output). Never call cl2000 directly.

CONFIG ?= CPU1_FLASH

CCS_ROOT       ?= D:/ti/ccs2101/ccs
CGT_ROOT       ?= $(CCS_ROOT)/tools/compiler/ti-cgt-c2000_25.11.1.LTS
C2000WARE_ROOT ?= D:/ti/ccs2101/C2000Ware_26_01_00_00

PROJECT_NAME := launchxl-f280039c-led-chaser
BUILD_DIR    := $(CONFIG)
SOURCE_DIR   := source
DEVICE_DIR   := device

CC       := "$(CGT_ROOT)/bin/cl2000"
LD       := "$(CGT_ROOT)/bin/cl2000"

DEVICE_INC := $(C2000WARE_ROOT)/device_support/f28003x/common/include
DLIB_DIR   := $(C2000WARE_ROOT)/driverlib/f28003x/driverlib
DLIB_LIB   := $(DLIB_DIR)/ccs/Debug/driverlib.lib

CFLAGS := -v28 -ml -mt --cla_support=cla2 --float_support=fpu32 \
          --tmu_support=tmu0 --vcu_support=vcrc -Ooff --abi=eabi \
          --gen_func_subsections=on --diag_warning=225 --diag_suppress=10063 \
          --display_error_number --diag_wrap=off --preproc_with_compile \
          --define=_LAUNCHXL_F280039C --define=DEBUG

INCLUDES := --include_path="$(SOURCE_DIR)" \
            --include_path="$(DEVICE_INC)" \
            --include_path="$(DLIB_DIR)" \
            --include_path="$(CGT_ROOT)/include"

ifeq ($(CONFIG),CPU1_FLASH)
  CFLAGS   += --define=_FLASH
  LINKER_CMD := 28003x_led_chaser_flash_lnk.cmd
else
  LINKER_CMD := 28003x_led_chaser_ram_lnk.cmd
endif

# --args=0x200: defines __c_args__ (argc/argv storage). REQUIRED for loadti:
# loadti calls memory.loadProgram(file, argvArgs) which needs the symbol, and
# fails with "SEVERE: __c_args__ not defined ... Error code #4011" without it.
# DSS memory.loadProgram(file) without args (hil_test.js) does NOT need it.
# Keep this flag when copying the Makefile to new projects.
LDFLAGS := -z -m"$(BUILD_DIR)/$(PROJECT_NAME).map" \
           --stack_size=0x200 --heap_size=0x100 --warn_sections \
           -i"$(CGT_ROOT)/lib" -i"$(C2000WARE_ROOT)" -i"$(CGT_ROOT)/include" \
           --reread_libs --display_error_number --diag_wrap=off \
           --entry_point=code_start --rom_model \
           --args=0x200 \
           --xml_link_info="$(BUILD_DIR)/$(PROJECT_NAME)_linkInfo.xml" \
           -o "$(BUILD_DIR)/$(PROJECT_NAME).out"

APP_SRCS := $(wildcard $(SOURCE_DIR)/*.c)
DEVICE_SRCS := $(DEVICE_DIR)/f28003x_codestartbranch.asm

APP_OBJS := $(patsubst $(SOURCE_DIR)/%.c,$(BUILD_DIR)/%.obj,$(APP_SRCS))
DEVICE_OBJS := $(patsubst $(DEVICE_DIR)/%.asm,$(BUILD_DIR)/%.obj,$(DEVICE_SRCS))
OBJS := $(APP_OBJS) $(DEVICE_OBJS)

# Header dependency tracking — see ADR 0007.
#
# cl2000's --preproc_dependency does write .d files, but their target and
# prerequisite paths use BACKSLASHES (CPU1_FLASH\self_test.obj: source\led_pattern.h)
# while this Makefile's pattern-rule target uses FORWARD slashes
# (CPU1_FLASH/self_test.obj). GNU make treats those as two DIFFERENT targets, so
# the obvious '-include $(DEPS)' silently does nothing: editing a header rebuilt
# nothing and a STALE image shipped. Proven 2026-10-18 by a fault-injection test:
# a deliberately corrupted LED_PATTERN_REF_FNV1A never reached the target, so the
# self test still passed and the negative-path check reported a false PASS.
#
# Fix: depend on the local headers directly. Separator-proof and deterministic.
# This project has <10 sources, so the extra rebuilds cost nothing.
HEADERS := $(wildcard $(SOURCE_DIR)/*.h)

.PHONY: all clean dirs

all: dirs $(BUILD_DIR)/$(PROJECT_NAME).out

# NOTE: gmake on Windows uses cmd.exe as the shell - no unix mkdir -p / rm -rf.
dirs:
	@if not exist "$(subst /,\,$(BUILD_DIR))" mkdir "$(subst /,\,$(BUILD_DIR))"

# $(HEADERS) here is the ADR 0007 fix - without it, header edits are ignored.
$(BUILD_DIR)/%.obj: $(SOURCE_DIR)/%.c $(HEADERS) | dirs
	@echo 'C2000 Compiler - building file: "$<"'
	$(CC) $(CFLAGS) $(INCLUDES) --preproc_dependency="$(BUILD_DIR)/$(basename $(<F)).d" --obj_directory="$(BUILD_DIR)" "$<"

$(BUILD_DIR)/%.obj: $(DEVICE_DIR)/%.asm | dirs
	@echo 'C2000 Assembler - building file: "$<"'
	$(CC) $(CFLAGS) $(INCLUDES) --obj_directory="$(BUILD_DIR)" "$<"

# NOTE: the .out target depends on the Makefile itself - otherwise editing
# LDFLAGS (e.g. --args=) would NOT trigger a relink and stale images ship.
$(BUILD_DIR)/$(PROJECT_NAME).out: $(OBJS) $(LINKER_CMD) Makefile
	@echo 'C2000 Linker - building target: "$@"'
	$(LD) $(CFLAGS) $(LDFLAGS) $(OBJS) "$(LINKER_CMD)" "$(DLIB_LIB)" -llibc.a
	@echo 'Finished building target: "$@"'

clean:
	@if exist "$(subst /,\,$(BUILD_DIR))" rmdir /s /q "$(subst /,\,$(BUILD_DIR))"
	@echo 'cleaned $(BUILD_DIR)'
