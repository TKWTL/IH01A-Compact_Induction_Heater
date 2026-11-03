#ifndef IH01A_HEATER_SETTINGS_H
#define IH01A_HEATER_SETTINGS_H
#include "core/ui.h"
/* Settings -> Heater: direct user controls and live diagnostics. */
void HeaterSettings_AddSettings(ui_page_t *settings_page);
void HeaterSettings_Poll(void);
#endif
