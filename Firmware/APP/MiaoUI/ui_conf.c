/* IH01A MiaoUI root: Main, Settings, Data, About.
 * Main/Data are deliberately read-only placeholders: no heater actions.
 * The current ASCII font is used until the Chinese glyph port.
 */
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"
#include "framework/pm_ui_register.h"

static ui_page_t s_root, s_settings;
static ui_item_t s_main, s_settings_item, s_data, s_about, s_back;

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
static void IH_DataPage(ui_t *ui)
{
    (void)ui;
    Disp_ClearBuffer();
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(2u, 12u, "IH01A Data");
    Disp_DrawStr(2u, 28u, "No live data");
    Disp_DrawStr(2u, 44u, "Sensors: Pending");
    Disp_DrawStr(2u, 60u, "EXIT: Back");
    Disp_SendBuffer();
}
void Create_MenuTree(ui_t *ui)
{
    (void)ui;
    AddPage("[Menu]", &s_root, UI_PAGE_ICON, 0);
    AddItem("-Main", UI_ITEM_WORD, img_dashboard, &s_main,
            &s_root, 0, IH_MainPage);
    AddItem("-Settings", UI_ITEM_PARENTS, img_configuration,
            &s_settings_item, &s_root, &s_settings, 0);
    AddItem("-Data", UI_ITEM_WORD, img_type_c1, &s_data,
            &s_root, 0, IH_DataPage);
    AddItem("-About", UI_ITEM_WORD, img_user_account, &s_about,
            &s_root, 0, 0);
    AddPage("[Settings]", &s_settings, UI_PAGE_TEXT, &s_root);
    AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_back,
            &s_settings, &s_root, 0);
    PM_UI_AddSleepSettingsItems(&s_settings);
    Add_Disp_Items(&s_settings);
}
void MiaoUi_Setup(ui_t *ui) { Create_UI(ui, &s_main); }
