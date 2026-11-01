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
