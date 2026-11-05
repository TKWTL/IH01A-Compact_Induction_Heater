/* IH01A UI_OFF policy regression: manual BACK must beat a frozen idle
 * countdown / No Auto Sleep; generic activity must not cause an OLED blink.
 * Uses real PM controller, policy and timer with only device hooks mocked. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "pm_api.h"
#include "pm_device.h"
#include "pm_sleep_timer.h"

static uint32_t s_now,s_prepared,s_resumed;
uint32_t TIME_Millis(void){return s_now;}
void pm_device_register_all(void){}
void pm_device_prepare_all(void){++s_prepared;}
void pm_device_suspend_all(void){}
void pm_device_resume_all(void){++s_resumed;}

int main(void)
{
    pm_api_init();
    assert(!pm_api_is_sleeping() && s_prepared==0u);
    assert(pm_sleep_timer_left_ms()==60000u);

    /* Output activity freezes the AUTOMATIC countdown. */
    s_now=10000u;
    pm_api_set_sleep_block(PM_BLOCK_HEATER,1u);
    pm_api_poll();
    assert(!pm_api_is_sleeping());
    s_now=90000u;
    pm_api_poll();
    assert(!pm_api_is_sleeping());
    assert(pm_sleep_timer_left_ms()==50000u);

    /* Root BACK requests manual screen-off despite a held block bit. */
    pm_api_force_sleep();
    assert(pm_api_is_sleeping()); /* screen-off transition is synchronous */
    pm_api_poll();
    assert(pm_api_is_sleeping());
    assert(s_prepared==1u && s_resumed==0u);
    assert(pm_sleep_timer_is_forced() && pm_sleep_timer_left_ms()==0u);
    assert(pm_api_get_block_mask()==PM_BLOCK_HEATER);

    /* Ordinary activity (such as periodic input-power refresh) must not
     * wake the OLED or cause the reported blink. */
    s_now+=40000u;
    pm_api_refresh_idle();
    pm_api_poll();
    assert(pm_api_is_sleeping() && s_prepared==1u && s_resumed==0u);

    /* Clearing a block is not a request to wake the screen. */
    pm_api_set_sleep_block(PM_BLOCK_HEATER,0u);
    pm_api_poll();
    assert(pm_api_is_sleeping() && s_resumed==0u);

    /* Debounced physical wake explicitly overrides the manual latch. */
    pm_api_wake_from_key();
    pm_api_poll();
    assert(!pm_api_is_sleeping());
    assert(!pm_sleep_timer_is_forced());
    assert(s_resumed==1u && pm_sleep_timer_left_ms()==60000u);

    /* Automatic timeout still works when the block is absent. */
    s_now+=59000u;pm_api_poll();
    assert(!pm_api_is_sleeping());
    s_now+=1001u;pm_api_poll();
    assert(pm_api_is_sleeping() && s_prepared==2u);
    pm_api_wake_from_key();pm_api_poll();
    assert(!pm_api_is_sleeping() && s_resumed==2u);

    /* No Auto Sleep is an automatic preference, not a ban on BACK. */
    pm_api_set_sleep_timeout(0);
    s_now+=200000u;pm_api_poll();
    assert(!pm_api_is_sleeping());
    assert(pm_sleep_timer_is_disabled());
    pm_api_force_sleep();pm_api_poll();
    assert(pm_api_is_sleeping() && s_prepared==3u);
    pm_api_refresh_idle();pm_api_poll();
    assert(pm_api_is_sleeping());
    pm_api_wake_from_key();pm_api_poll();
    assert(!pm_api_is_sleeping() && s_resumed==3u);

    /* A new configured timeout with a block still does not auto-sleep. */
    pm_api_set_sleep_timeout(30); /* API unit: seconds */
    pm_api_set_sleep_block(PM_BLOCK_HEATER,1u);
    pm_api_poll();
    s_now+=31000u;pm_api_poll();
    assert(!pm_api_is_sleeping());
    pm_api_set_sleep_block(PM_BLOCK_HEATER,0u);
    pm_api_poll();
    assert(!pm_api_is_sleeping());
    s_now+=30001u;pm_api_poll();
    assert(pm_api_is_sleeping() && s_prepared==4u);
    puts("PM manual sleep / blocked countdown: PASS");
    return 0;
}
