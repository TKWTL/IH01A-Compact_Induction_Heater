#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
# Windows(mingw) 的默认 snprintf 是 MSVCRT 语义:不按 n 截断、不保证 NUL,
# 会让 test_time 的 "size=1 时应输出空串" 断言假失败(ARM/armclang 上是符合 C99 的)。
# 该宏在 Linux/macOS 上无副作用。
CFLAGS="-std=c99 -Wall -Wextra -Werror -D__USE_MINGW_ANSI_STDIO=1"
OUT=${OUT:-${TMPDIR:-/tmp}}

build_run() {
    name=$1
    shift
    "$CC" $CFLAGS "$@" -o "$OUT/ih01a_${name}_test"
    "$OUT/ih01a_${name}_test"
}

build_run pm_timer -I Tests/host/stubs -I APP/framework \
 APP/framework/pm_sleep_timer.c Tests/host/test_pm_timer.c

build_run pm_manual -I Tests/host/stubs -I APP/framework -I APP \
 APP/framework/pm_sleep_timer.c APP/framework/pm_policy.c \
 APP/framework/pm_controller.c APP/framework/pm_api.c \
 Tests/host/test_pm_manual.c

build_run reset_diag -I Core/Inc Tests/host/test_ih_reset_diag.c

build_run mini_format -I BSP/Console \
 BSP/Console/mini_format.c Tests/host/test_mini_format.c

build_run pd_source_stats -I APP \
 APP/algorithm/pd_source_stats.c Tests/host/test_pd_source_stats.c

build_run time -I Tests/host/rtc_mock -I APP \
 APP/framework/time_service.c Tests/host/test_time.c

build_run pd_select -I APP \
 APP/algorithm/pd_select.c Tests/host/test_pd_select.c

build_run ntc_lut -I APP \
 APP/algorithm/ntc_lut.c Tests/host/test_ntc_lut.c

build_run temp_service -I Tests/host/temp_mock -I APP -I APP/PowerControl \
 APP/PowerControl/temp_service.c APP/algorithm/ntc_lut.c \
 Tests/host/test_temp_service.c

build_run beep -I APP \
 APP/framework/beep.c Tests/host/test_beep.c

build_run heater_ui_format -I APP Tests/host/test_heater_ui_format.c

build_run heater -I APP \
 APP/PowerControl/heater_control.c APP/PowerControl/heater_budget.c \
 Tests/host/test_heater_control.c

build_run conv_protection -DCONV_PROTECTION_HOST_TEST -I APP/PowerControl \
 APP/PowerControl/conv_protection.c Tests/host/test_conv_protection.c

build_run phase -I APP APP/PowerControl/conv_phase.c \
 Tests/host/test_conv_phase.c

build_run zcd -DCONV_ZCD_HOST_TEST -I APP/PowerControl \
 APP/PowerControl/conv_phase.c APP/PowerControl/conv_zcd.c \
 Tests/host/test_conv_zcd.c

build_run auto_frequency -DCONV_AUTO_FREQ_HOST_TEST -DCONV_ZCD_HOST_TEST \
 -I APP/PowerControl -I APP \
 APP/PowerControl/conv_auto_freq.c APP/PowerControl/conv_phase.c \
 APP/PowerControl/conv_zcd.c Tests/host/test_conv_auto_freq.c

build_run ct_current -I APP APP/PowerControl/conv_current.c \
 Tests/host/test_conv_current.c

build_run heater_cli_parse -I BSP/Console \
 BSP/Console/heater_cli_parse.c Tests/host/test_heater_cli_parse.c

build_run heater_cli_commands -DHEATER_CLI_HOST_TEST -I APP -I BSP/Console -I Tests/host \
 BSP/Console/heater_cli_parse.c BSP/Console/heater_cli.c \
 APP/PowerControl/conv_phase.c APP/PowerControl/conv_current.c \
 Tests/host/test_heater_cli_commands.c

# MCU integration smoke tests: the host unit tests above do not compile
# CubeMX modules, so enforce cross-module wiring and single DMA ISR owner.
grep -Fq 'uint8_t pan_detect_enabled;' APP/PowerControl/heater_control.h
grep -Fq 'uint8_t Heater_SetPanDetectEnabled(uint8_t enabled);' APP/PowerControl/heater_control.h
grep -Fq 'CONV_BENCH_COUNTDOWN' APP/PowerControl/conv_controller.h
grep -Fq 'extern DMA_HandleTypeDef hdma_tim1_ch2;' Core/Inc/tim.h
grep -Fq 'ConvController_PanProbeService(TIME_Millis());' APP/app_tasks.c
grep -Fq 'ConvController_PanProbeUpdateISR();' Core/Src/stm32g0xx_it.c
grep -Fq 'void DMA1_Channel2_3_IRQHandler(void)' APP/PowerControl/conv_controller.c
if grep -Fq 'void DMA1_Channel2_3_IRQHandler(void)' Core/Src/stm32g0xx_it.c; then
  echo 'ERROR: duplicate DMA1_Channel2_3_IRQHandler' >&2
  exit 1
fi
echo 'pan probe integration: PASS'
