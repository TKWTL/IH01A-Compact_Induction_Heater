/* IH01A MiaoUI root: Main, Settings, Statistics, About.
 * Main is a deliberately read-only placeholder: no heater actions.
 * Settings is split into Display (contrast / background / burn-in / flip) and
 * System (sleep options).  Statistics still only holds the empty "PD in" and
 * "System" sub-pages.
 * The current ASCII font is used until the Chinese glyph port.
 */
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"
#include "framework/pm_ui_register.h"

static ui_page_t s_root, s_settings, s_display, s_system;
static ui_page_t s_statistics, s_pd_in, s_stat_sys;
static ui_item_t s_main, s_settings_item, s_display_item, s_system_item,
                 s_statistics_item, s_pd_in_item, s_stat_sys_item, s_about;
static ui_item_t s_back, s_back_display, s_back_system,
                 s_back_statistics, s_back_pd_in, s_back_stat_sys;

void Create_Parameter(ui_t *ui) { Create_Disp_Parameters(ui); }

void Create_Text(ui_t *ui)
{
    static ui_text_t about_text;
    static ui_element_t about_element;
    (void)ui;
    about_text.ptr = "IH01A Framework\nSTM32G071\nSSD1315 / SPI\nExternal VCC\nHeater: DISABLED";
    about_text.font = UI_FONT;
    about_text.fontHight = UI_FONT_HIGHT;
    about_text.fontWidth = UI_FONT_WIDTH;
    about_element.text = &about_text;
    Create_element(&s_about, &about_element);
}
static void IH_MainPage(ui_t *ui)
{
    (void)ui;
    Disp_ClearBuffer();
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(2u, 12u, "IH01A Main");
    Disp_DrawStr(2u, 28u, "Layout: Pending");
    Disp_DrawStr(2u, 44u, "Heater: OFF");
    Disp_DrawStr(2u, 60u, "EXIT: Back");
    Disp_SendBuffer();
}
void Create_MenuTree(ui_t *ui)
{
    (void)ui;
    AddPage("[Menu]", &s_root, UI_PAGE_ICON, 0);
    AddItem("-Main", UI_ITEM_WORD, img_dashboard, &s_main, &s_root, 0, IH_MainPage);

    AddItem("-Settings", UI_ITEM_PARENTS, img_configuration, &s_settings_item, &s_root, &s_settings, 0);
    AddPage("[Settings]", &s_settings, UI_PAGE_TEXT, &s_root);
        AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_back, &s_settings, &s_root, 0);
        AddItem("-Display", UI_ITEM_PARENTS, 0, &s_display_item, &s_settings, &s_display, 0);
        AddPage("[Display]", &s_display, UI_PAGE_TEXT, &s_settings);
            AddItem("[Return]", UI_ITEM_RETURN, 0, &s_back_display, &s_display, &s_settings, 0);
            Add_Disp_Items(&s_display);
        AddItem("-System", UI_ITEM_PARENTS, 0, &s_system_item, &s_settings, &s_system, 0);
        AddPage("[System]", &s_system, UI_PAGE_TEXT, &s_settings);
            AddItem("[Return]", UI_ITEM_RETURN, 0, &s_back_system, &s_system, &s_settings, 0);
            PM_UI_AddSleepSettingsItems(&s_system);

    AddItem("-Statistics", UI_ITEM_PARENTS, img_statistics, &s_statistics_item, &s_root, &s_statistics, 0);
    AddPage("[Statistics]", &s_statistics, UI_PAGE_TEXT, &s_root);
        AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_back_statistics, &s_statistics, &s_root, 0);
        AddItem("-PD in", UI_ITEM_PARENTS, 0, &s_pd_in_item, &s_statistics, &s_pd_in, 0);
        AddPage("[PD in]", &s_pd_in, UI_PAGE_TEXT, &s_statistics);
            AddItem("[Return]", UI_ITEM_RETURN, 0, &s_back_pd_in, &s_pd_in, &s_statistics, 0);
        AddItem("-System", UI_ITEM_PARENTS, 0, &s_stat_sys_item, &s_statistics, &s_stat_sys, 0);
        AddPage("[System]", &s_stat_sys, UI_PAGE_TEXT, &s_statistics);
            AddItem("[Return]", UI_ITEM_RETURN, 0, &s_back_stat_sys, &s_stat_sys, &s_statistics, 0);

    AddItem("-About", UI_ITEM_WORD, img_user_account, &s_about, &s_root, 0, 0);
}
void MiaoUi_Setup(ui_t *ui) { Create_UI(ui, &s_main); }
