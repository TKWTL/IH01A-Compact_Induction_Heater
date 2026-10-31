#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
"$CC" -std=c99 -Wall -Wextra -Werror -I Tests/host/stubs -I APP/framework \
 APP/framework/pm_sleep_timer.c Tests/host/test_pm_timer.c -o /tmp/ih01a_pm_timer_test
/tmp/ih01a_pm_timer_test
