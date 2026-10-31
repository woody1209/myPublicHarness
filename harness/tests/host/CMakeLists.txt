cmake_minimum_required(VERSION 3.20)

# Host-side unit tests for PURE LOGIC firmware modules.
# Hardware-dependent code must NOT reach this layer (see docs/30).
#
# Framework: harness/tests/host/framework/fw_test.{h,c} - self-contained, no
# FetchContent, no network. (C3M3-harness uses Unity via FetchContent; this
# branch keeps the suite runnable on machines without cmake network access.)
#
# v2 (spec 0002 T-005): one static library + one runner executable PER PROJECT,
# driven by harness/tests/host/modules.json (projects[]).
# harness/tools/static-check.ps1 fails when any block below, the per-project
# runners, or that manifest disagree.
project(f280039c_harness_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(fw_test_framework STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/fw_test.c"
)
target_include_directories(fw_test_framework PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/framework"
)

# ---- project: launchxl-f280039c-led-chaser -----------------------------------
set(FW_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../projects/launchxl-f280039c-led-chaser/source"
    CACHE PATH "led-chaser firmware source root")

add_library(fw_logic STATIC
    "${FW_SOURCE_DIR}/led_pattern.c"
    "${FW_SOURCE_DIR}/fmt.c"
    "${FW_SOURCE_DIR}/self_test.c"
)
target_include_directories(fw_logic PUBLIC "${FW_SOURCE_DIR}")

add_executable(test_runner
    runner/test_runner.c
    test/test_led_pattern.c
    test/test_fmt.c
)
target_include_directories(test_runner PRIVATE "${FW_SOURCE_DIR}")
target_link_libraries(test_runner PRIVATE fw_logic fw_test_framework)

# ---- project: launchxl-f280039c-motor-pwm ------------------------------------
# Own source root variable: the led-chaser FW_SOURCE_DIR above is already baked
# into fw_logic's absolute source list, but a second set() would silently make
# the file lie about what fw_logic was built from.
set(FW_SOURCE_DIR_MP "${CMAKE_CURRENT_SOURCE_DIR}/../../../projects/launchxl-f280039c-motor-pwm/source"
    CACHE PATH "motor-pwm firmware source root")

add_library(fw_logic_mp STATIC
    "${FW_SOURCE_DIR_MP}/angle_gen.c"
    "${FW_SOURCE_DIR_MP}/svpwm.c"
    "${FW_SOURCE_DIR_MP}/fmt.c"
    "${FW_SOURCE_DIR_MP}/self_test.c"
)
target_include_directories(fw_logic_mp PUBLIC "${FW_SOURCE_DIR_MP}")

add_executable(test_runner_mp
    runner/test_runner_mp.c
    test/test_angle_gen.c
    test/test_svpwm.c
    test/test_mp_selftest.c
)
target_include_directories(test_runner_mp PRIVATE "${FW_SOURCE_DIR_MP}")
target_link_libraries(test_runner_mp PRIVATE fw_logic_mp fw_test_framework)

# ---- project: launchxl-f280039c-full-platform ---------------------------------
# Derived from the F280039C-harness baseline (2026-10-31). Own source root
# variable for the same reason as motor-pwm above.
set(FW_SOURCE_DIR_FP "${CMAKE_CURRENT_SOURCE_DIR}/../../../projects/launchxl-f280039c-full-platform/source"
    CACHE PATH "full-platform firmware source root")

add_library(fw_logic_fp STATIC
    "${FW_SOURCE_DIR_FP}/scheduler.c"
    "${FW_SOURCE_DIR_FP}/health.c"
    "${FW_SOURCE_DIR_FP}/self_test.c"
    "${FW_SOURCE_DIR_FP}/fmt.c"
    "${FW_SOURCE_DIR_FP}/led_pattern.c"
    "${FW_SOURCE_DIR_FP}/adc_stat.c"
    "${FW_SOURCE_DIR_FP}/math_ref.c"
    "${FW_SOURCE_DIR_FP}/frame_gen.c"
)
target_include_directories(fw_logic_fp PUBLIC "${FW_SOURCE_DIR_FP}")

add_executable(test_runner_fp
    runner/test_runner_fp.c
    test/test_scheduler.c
    test/test_health.c
    test/test_fp_selftest.c
    test/test_adc_stat.c
    test/test_math_ref.c
    test/test_frame_gen.c
)
target_include_directories(test_runner_fp PRIVATE "${FW_SOURCE_DIR_FP}")
target_link_libraries(test_runner_fp PRIVATE fw_logic_fp fw_test_framework)

# ---- ctest wiring --------------------------------------------------------------
enable_testing()
add_test(NAME host_tests COMMAND test_runner)
add_test(NAME host_tests_mp COMMAND test_runner_mp)
add_test(NAME host_tests_fp COMMAND test_runner_fp)
