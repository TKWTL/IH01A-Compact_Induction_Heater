/* IH01A input adapter: L=UP, R=DOWN, ENTR=ENTER, EXIT=BACK. */
#include "indev/indevDriver.h"
#include "buttons.h"
UI_ACTION indevScan(void)
{
    uint8_t key=Buttons_TakeAction();
    if(key==IH_KEY_EXIT) return UI_ACTION_BACK;
    if(key==IH_KEY_ENTR) return UI_ACTION_ENTER;
    if(key==IH_KEY_L) return UI_ACTION_UP;
    if(key==IH_KEY_R) return UI_ACTION_DOWN;
    return UI_ACTION_NONE;
}
