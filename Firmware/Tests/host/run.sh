#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
"$CC" -std=c99 -Wall -Wextra -Werror -I Tests/host/stubs -I APP/framework \
 APP/framework/pm_sleep_timer.c Tests/host/test_pm_timer.c -o /tmp/ih01a_pm_timer_test
/tmp/ih01a_pm_timer_test

"$CC" -std=c99 -Wall -Wextra -Werror -I BSP/Console \
 BSP/Console/mini_format.c Tests/host/test_mini_format.c -o /tmp/ih01a_mini_format_test
/tmp/ih01a_mini_format_test

"$CC" -std=c99 -Wall -Wextra -Werror -I APP \
 APP/pd_source_stats.c Tests/host/test_pd_source_stats.c -o /tmp/ih01a_pd_source_stats_test
/tmp/ih01a_pd_source_stats_test
