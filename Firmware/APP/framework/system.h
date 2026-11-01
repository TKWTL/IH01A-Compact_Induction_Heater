#ifndef IH01A_FRAMEWORK_SYSTEM_H
#define IH01A_FRAMEWORK_SYSTEM_H

#include "ui_conf.h"

void System_Reset(void);
void System_AddSettingsItems(ui_page_t *page);
void System_AddStatisticsItems(ui_page_t *page);
void System_Poll(void);

#endif
