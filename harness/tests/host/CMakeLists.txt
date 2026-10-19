cmake_minimum_required(VERSION 3.20)

# Host-side unit tests for PURE LOGIC firmware modules.
# Hardware-dependent code must NOT reach this layer (see docs/30).
#
# Framework: harness/tests/host/framework/fw_test.{h,c} - self-contained, no
# FetchContent, no network. (C3M3-harness uses Unity via FetchContent; this
# branch keeps the suite runnable on machines without cmake network access.)
project(f280039c_harness_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- modules under test (firmware sources compiled for the host) ------------
# Declared truth: harness/tests/host/modules.json. harness/tools/static-check.ps1
# fails when this block, the test_runner sources below, or that manifest disagree.
set(FW_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../projects/launchxl-f280039c-led-chaser/source"
    CACHE PATH "Firmware source root")

add_library(fw_test_framework STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/fw_test.c"
)
target_include_directories(fw_test_framework PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/framework")

add_library(fw_logic STATIC
    "${FW_SOURCE_DIR}/led_pattern.c"
    "${FW_SOURCE_DIR}/fmt.c"
    "${FW_SOURCE_DIR}/self_test.c"
)
target_include_directories(fw_logic PUBLIC "${FW_SOURCE_DIR}")

# ---- test runner --------------------------------------------------------------
add_executable(test_runner
    runner/test_runner.c
    test/test_led_pattern.c
    test/test_fmt.c
)
target_include_directories(test_runner PRIVATE "${FW_SOURCE_DIR}")
target_link_libraries(test_runner PRIVATE fw_logic fw_test_framework)

enable_testing()
add_test(NAME host_tests COMMAND test_runner)
