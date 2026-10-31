/* UI_OFF-only PM. MCU node is present for a future deep-sleep port,
 * but must NEVER enter STOP/STANDBY while USBPD/heater logic is running.
 */
#include "pm_device.h"
#include "display/dispDriver.h"

static void oled_prepare(void *context)
{
    (void)context;
    Disp_SetPowerSave(1u);
}
static void oled_resume(void *context)
{
    (void)context;
    Disp_SetPowerSave(0u);
    Disp_SendBuffer();
}
static const pm_device_node_t s_oled = {
    "ssd1315", oled_prepare, 0, oled_resume, 0, 10, 0, 0
};
static const pm_device_node_t s_mcu = {
    "stm32g071", 0, 0, 0, 0, 250, 0, 0
};
void pm_device_register_builtin(void)
{
    (void)pm_device_register(&s_oled);
    (void)pm_device_register(&s_mcu);
}
