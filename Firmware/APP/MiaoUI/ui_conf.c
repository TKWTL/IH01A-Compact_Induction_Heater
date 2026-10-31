/* IH01A MiaoUI entry: a safe bring-up UI. Heater PWM is never enabled.
 * All names remain ASCII until the planned UTF-8/font-subsetting milestone. */
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"
#include "framework/pm_ui_register.h"

static ui_page_t s_root, s_settings;
static ui_item_t s_status, s_settings_item, s_back, s_about;

void Create_Parameter(ui_t *ui) { Create_Disp_Parameters(ui); }

void Create_Text(ui_t *ui)
{
    static ui_text_t text;
    static ui_element_t element;
    (void)ui;
    text.ptr="IH01A Framework\nSTM32G071 64MHz\nSSD1315 SPI OLED\nExternal VCC\nHeater: DISABLED";
    text.font=UI_FONT;
    text.fontHight=UI_FONT_HIGHT;
    text.fontWidth=UI_FONT_WIDTH;
    element.text=&text;
    Create_element(&s_about,&element);
}

static void IH_StatusPage(ui_t *ui)
{
    (void)ui;
    Disp_ClearBuffer();
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(2u,12u,"IH01A / G071");
    Disp_DrawStr(2u,28u,"SSD1315: SPI");
    Disp_DrawStr(2u,44u,"HEATER: OFF");
    Disp_DrawStr(2u,60u,"EXIT: Back");
    Disp_SendBuffer();
}
void Create_MenuTree(ui_t *ui)
{
    (void)ui;
    AddPage("[Menu]", &s_root, UI_PAGE_ICON, 0);
    AddItem("-Status",UI_ITEM_WORD,img_dashboard,&s_status,&s_root,0,IH_StatusPage);
    AddItem("-Settings",UI_ITEM_PARENTS,img_configuration,
            &s_settings_item,&s_root,&s_settings,0);
    AddPage("[Settings]",&s_settings,UI_PAGE_TEXT,&s_root);
    AddItem("[Menu]",UI_ITEM_RETURN,0,&s_back,&s_settings,&s_root,0);
    PM_UI_AddSleepSettingsItems(&s_settings);
    Add_Disp_Items(&s_settings);
    AddItem("-About",UI_ITEM_WORD,img_user_account,&s_about,&s_root,0,0);
}
void MiaoUi_Setup(ui_t *ui) { Create_UI(ui,&s_status); }
