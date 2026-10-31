# Makefile — CPU1 (C28x) build for projects/launchxl-f280039c-full-platform
#
# Invoked ONLY through harness/tools/ccs.ps1 (AGENTS.md §1):
#   gmake all CONFIG=CPU1_FLASH CCS_ROOT=<ccs> CGT_ROOT=<cgt> C2000WARE_ROOT=<sdk>
#
# Skeleton copied from the led-chaser reference project (spec 0002 T-001);
# the four mandatory bits are preserved: --args=0x200 (ADR 0004), .out
# depends on Makefile itself, .obj depends on HEADERS (ADR 0007), cmd.exe
# compatible recipes (no mkdir -p / rm -rf).

CONFIG ?= CPU1_FLASH

CCS_ROOT       ?= D:/ti/ccs2101/ccs
CGT_ROOT       ?= $(CCS_ROOT)/tools/compiler/ti-cgt-c2000_25.11.1.LTS
C2000WARE_ROOT ?= D:/ti/ccs2101/C2000Ware_26_01_00_00

PROJECT_NAME := launchxl-f280039c-full-platform
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
  LINKER_CMD := 28003x_full_platform_flash_lnk.cmd
else
  LINKER_CMD := 28003x_full_platform_ram_lnk.cmd
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

# Driverlib externs needed by the M3 comm subsystems: the prebuilt
# driverlib.lib does not export them on this SDK/CGT pairing (unresolved
# SPI_disableTestMode at link, 2026-10-25), so the modules are compiled
# straight from C2000Ware with the same flags.
DLIB_COMPILED := spi can i2c lin fsi dma
# Objects are prefixed `dlib_` so they can never collide with an application
# module of the same basename, and the rule below passes --output_file=$@ so
# the produced file name EQUALS the make target name. Without --output_file,
# cl2000 names the object after the source basename (CPU1_FLASH/spi.obj) and
# gmake prints "pattern recipe did not update peer target" every run (A11).
DLIB_OBJS := $(patsubst %,$(BUILD_DIR)/dlib_%.obj,$(DLIB_COMPILED))

CLA_SRCS := $(SOURCE_DIR)/cla_app.cla
CLA_OBJS := $(BUILD_DIR)/cla_app.obj

APP_OBJS := $(patsubst $(SOURCE_DIR)/%.c,$(BUILD_DIR)/%.obj,$(APP_SRCS))
DEVICE_OBJS := $(patsubst $(DEVICE_DIR)/%.asm,$(BUILD_DIR)/%.obj,$(DEVICE_SRCS))
OBJS := $(APP_OBJS) $(CLA_OBJS) $(DLIB_OBJS) $(DEVICE_OBJS)

# Header dependency tracking — see ADR 0007. cl2000's .d files use
# backslash paths that gmake's -include silently ignores, so every object
# depends on the local headers directly. Correctness beats rebuild speed.
HEADERS := $(wildcard $(SOURCE_DIR)/*.h)

.PHONY: all clean dirs

all: dirs $(BUILD_DIR)/$(PROJECT_NAME).out

# NOTE: gmake on Windows uses cmd.exe as the shell - no unix mkdir -p / rm -rf.
dirs:
	@if not exist "$(subst /,\,$(BUILD_DIR))" mkdir "$(subst /,\,$(BUILD_DIR))"

$(BUILD_DIR)/%.obj: $(SOURCE_DIR)/%.c $(HEADERS) | dirs
	@echo 'C2000 Compiler - building file: "$<"'
	$(CC) $(CFLAGS) $(INCLUDES) --preproc_dependency="$(BUILD_DIR)/$(basename $(<F)).d" --obj_directory="$(BUILD_DIR)" "$<"

$(BUILD_DIR)/%.obj: $(DEVICE_DIR)/%.asm | dirs
	@echo 'C2000 Assembler - building file: "$<"'
	$(CC) $(CFLAGS) $(INCLUDES) --obj_directory="$(BUILD_DIR)" "$<"

$(BUILD_DIR)/cla_app.obj: $(SOURCE_DIR)/cla_app.cla $(HEADERS) | dirs
	@echo 'CLA Compiler - building file: "$<"'
	$(CC) $(CFLAGS) $(INCLUDES) --define=CLA_C --obj_directory="$(BUILD_DIR)" "$<"

# Single-target pattern rule + explicit --output_file: the recipe always
# creates exactly $@, so gmake stops force-rebuilding these every run.
$(BUILD_DIR)/dlib_%.obj: $(DLIB_DIR)/%.c $(HEADERS) | dirs
	@echo 'C2000 Compiler - building driverlib module: "$<"'
	$(CC) $(CFLAGS) $(INCLUDES) --obj_directory="$(BUILD_DIR)" --temp_directory="$(BUILD_DIR)" --output_file="$@" "$<"

# NOTE: the .out target depends on the Makefile itself - otherwise editing
# LDFLAGS (e.g. --args=) would NOT trigger a relink and stale images ship.
$(BUILD_DIR)/$(PROJECT_NAME).out: $(OBJS) $(LINKER_CMD) Makefile
	@echo 'C2000 Linker - building target: "$@"'
	$(LD) $(CFLAGS) $(LDFLAGS) $(OBJS) "$(LINKER_CMD)" "$(DLIB_LIB)" -llibc.a
	@echo 'Finished building target: "$@"'

clean:
	@if exist "$(subst /,\,$(BUILD_DIR))" rmdir /s /q "$(subst /,\,$(BUILD_DIR))"
	@echo 'cleaned $(BUILD_DIR)'
