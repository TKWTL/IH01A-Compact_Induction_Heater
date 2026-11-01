/* IH01A MiaoUI root: Main, Settings, Statistics, About.
 * Main is a deliberately read-only placeholder: no heater actions.
 * Settings is split into Display (contrast / background / burn-in / flip) and
 * System (sleep options). Statistics/PD in shows live Source PDOs;
 * Statistics/System displays ADC pin voltages and MCU temperature.
 * The current ASCII font is used until the Chinese glyph port.
 */
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"
#include "framework/pm_ui_register.h"
#include "pd_source_stats.h"
#include "dashboard.h"
#include "framework/system.h"
#include <stdint.h>

static ui_page_t s_root, s_settings, s_display, s_system;
static ui_page_t s_statistics, s_pd_in, s_stat_sys;
static ui_item_t s_main, s_settings_item, s_display_item, s_system_item,
                 s_statistics_item, s_pd_in_item, s_stat_sys_item, s_about;
static ui_item_t s_back, s_back_display, s_back_system,
                 s_back_statistics, s_back_pd_in, s_back_stat_sys;

/* Static PAGE_TEXT content: capacity for every USB-PD Source PDO.
 * Row strings persist for the lifetime of the menu; no dynamic allocation. */
#define PD_UI_ROW_CAPACITY 32u
static ui_item_t s_pd_rows[PD_SOURCE_MAX_PDOS];
static char s_pd_text[PD_SOURCE_MAX_PDOS][PD_UI_ROW_CAPACITY];
static uint32_t s_pd_revision = 0xFFFFFFFFu;

void IH_Statistics_Poll(void)
{
    uint32_t revision = PD_SourceStats_Revision();
    PowerDashboard_Poll();
    System_Poll();
    uint8_t i;

    if(revision == s_pd_revision)
        return;

    for(i = 0u; i < PD_SOURCE_MAX_PDOS; i++)
    {
        PD_SourceStats_FormatRow(i, s_pd_text[i], sizeof(s_pd_text[i]));
        s_pd_rows[i].x = UI_PAGE_INIT_X;
#if ( UI_TITLE_ROLL == 1 )
        s_pd_rows[i].rollState = UI_ITEM_ROLL_STOP;
#endif
    }
    s_pd_revision = revision;
}

/* PD rows are informational only; selecting one exits immediately. */
static void IH_PD_RowNoop(ui_t *ui)
{
    if(ui != 0 && ui->action == UI_ACTION_NONE)
        ui->action = UI_ACTION_BACK;
}

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
    uint8_t i;
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
            /* Last option under Settings/System, after sleep settings. */
            System_AddSettingsItems(&s_system);

    AddItem("-Statistics", UI_ITEM_PARENTS, img_statistics, &s_statistics_item, &s_root, &s_statistics, 0);
    AddPage("[Statistics]", &s_statistics, UI_PAGE_TEXT, &s_root);
        AddItem("[Menu]", UI_ITEM_RETURN, 0, &s_back_statistics, &s_statistics, &s_root, 0);
        AddItem("-PD in", UI_ITEM_PARENTS, 0, &s_pd_in_item, &s_statistics, &s_pd_in, 0);
        AddPage("[PD in]", &s_pd_in, UI_PAGE_TEXT, &s_statistics);
            s_pd_in.wideText = 1u;  /* allow complete Fixed 5.0V 3.0A labels */
            AddItem("[Return]", UI_ITEM_RETURN, 0, &s_back_pd_in, &s_pd_in, &s_statistics, 0);
            IH_Statistics_Poll();
            for(i = 0u; i < PD_SOURCE_MAX_PDOS; i++)
                AddItem(s_pd_text[i], UI_ITEM_WORD, 0,
                        &s_pd_rows[i], &s_pd_in, 0, IH_PD_RowNoop);
        AddItem("-System", UI_ITEM_PARENTS, 0, &s_stat_sys_item, &s_statistics, &s_stat_sys, 0);
        AddPage("[System]", &s_stat_sys, UI_PAGE_TEXT, &s_statistics);
            s_stat_sys.wideText = 1u;
            AddItem("[Return]", UI_ITEM_RETURN, 0, &s_back_stat_sys, &s_stat_sys, &s_statistics, 0);
            /* Show elapsed runtime before the ADC data rows. */
            System_AddStatisticsItems(&s_stat_sys);
            PowerDashboard_AddItems(&s_stat_sys);

    AddItem("-About", UI_ITEM_WORD, img_user_account, &s_about, &s_root, 0, 0);
}

void MiaoUi_Setup(ui_t *ui) { Create_UI(ui, &s_main); }

/* ---- 菜单配置推荐规范(由本文件归纳,新增页面照此写) ----
 *
 * 命名
 *   页面      s_<页面名>        如 s_root / s_settings / s_display
 *   菜单项    s_<所在页>_item   如 s_display_item(打开子页的那一项)
 *   返回项    s_back_<所在页>   如 s_back_display(位于 [Display] 页内)
 *   标题允许重名,标识符不允许;同名页面加限定,如统计页的 s_stat_sys
 *
 * 添加顺序(深度优先,子页紧跟它的入口项)
 *   1. AddPage(父页)
 *   2. 父页的 AddItem(打开子页的那一项写在 AddPage(子页) 之前)
 *   3. AddPage(子页) 及其全部 AddItem
 *   4. 同一页面内返回项排在最前,其余项按显示顺序追加
 *
 * 标签
 *   页面名 "[Name]"   子页入口 "-Name"   返回项 "[Return]"(上一级) / "[Menu]"(根)
 *   只有 ICON 页用图标;TEXT 页图标位传 0
 *
 * 缩进(缩进即层级)
 *   根页项目 4 格 → 一级子页项目 8 格 → 二级子页项目 12 格
 *
 * 内容填充
 *   成组的控件项由模块函数一次性挂入: Add_Disp_Items(&page)、
 *   PM_UI_AddSleepSettingsItems(&page);占位页只放返回项。
 */
