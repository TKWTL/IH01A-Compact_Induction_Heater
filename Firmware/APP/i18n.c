/* Static UI translation dictionary; no heap and no key rewriting.
 * Keep English literals unchanged in AddItem/AddPage/data->name.
 * Leave numeric, fault and CLI telemetry strings untranslated until reviewed.
 */
#include "i18n.h"
#include "MiaoUI/fonts/fonts.h"
#include <string.h>
typedef struct { const char *english; const char *zh_hans; } translation_t;
static uint8_t s_chinese;
static const translation_t s_dictionary[] = {
    { "-Main", "主界面" },
    { "-Settings", "设置" },
    { "-Statistics", "数据" },
    { "-About", "关于" },
    { "[Menu]", "主菜单" },
    { "[Settings]", "设置" },
    { "[Return]", "返回" },
    { "-Display", "显示" },
    { "[Display]", "显示" },
    { "-System", "系统" },
    { "[System]", "系统" },
    { "-Source PDO", "电源档位" },
    { "[Source PDO]", "电源档位" },
    { "-Heater", "加热控制" },
    { "[Heater]", "加热控制" },
    { "-PD", "PD供电" },
    { "[PD]", "PD供电" },
    { " Retry PD", " 重试PD" },
    { " Contrast", " 对比度" },
    { " Background", " 反色显示" },
    { " Burn-in Test", " 全屏测试" },
    { " Screen Flip", " 屏幕翻转" },
    { " PC6 Enable", " 输出使能" },
    { " Pan Detect", " 检锅" },
    { " Phase deg", " 移相角" },
    { " Auto Freq", " 自动调频" },
    { " Freq kHz", " 频率kHz" },
    { " Click Beep", " 按键音" },
    { " Mute Beep", " 静音" },
    { " Play Music", " 播放音乐" },
    { " System Reset", " 系统重启" },
    { "-Sleep Options", "休眠选项" },
    { "[Sleep]", "休眠选项" },
    { "[Home]", "返回" },
    { " No Auto Sleep", " 禁用自动休眠" },
    { " 15s", " 15秒" },
    { " 30s", " 30秒" },
    { " 1min", " 1分钟" },
    { " 2min", " 2分钟" },
    { " 5min", " 5分钟" },
    { " 10min", " 10分钟" },
    { " 15min", " 15分钟" },
    { " 30min", " 30分钟" },
    { " Language", " 中文界面" },
    { "Contrast", "对比度" },
    { "Phase deg", "移相角" },
    { "Auto Freq", "自动调频" },
    { "Freq kHz", "频率kHz" },
    { "Pan Detect", "检锅" },
    { "IH01A Framework\nSTM32G071\nSSD1315 / SPI\nExternal VCC\nHeater: DISABLED", "IH01A 技术验证\nSTM32G071\nSSD1315 / SPI\n外部供电\n加热需安全联锁" },
};
void IH_I18n_SetChinese(uint8_t enable) { s_chinese = enable ? 1u : 0u; }
uint8_t IH_I18n_IsChinese(void) { return s_chinese; }
const uint8_t *IH_I18n_Font(void) {
    return s_chinese ? font_fusion_pixel_12_zh : font_menu_main_h12w6;
}
const char *IH_I18n_Text(const char *english) {
    unsigned i;
    if(english == 0) return "";
    if(!s_chinese) return english;
    for(i=0u;i<sizeof(s_dictionary)/sizeof(s_dictionary[0]);++i)
        if(strcmp(english,s_dictionary[i].english)==0)
            return s_dictionary[i].zh_hans;
    return english;
}
