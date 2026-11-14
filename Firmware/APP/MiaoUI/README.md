# IH01A MiaoUI

SSD1315 128×64 OLED、四键菜单与中英双语显示。基于 DPC27A 的定点 MiaoUI（MIT），使用裁剪版 U8g2。

- 主菜单：Main / Settings / Statistics / About；设置和状态页按固件功能增补 Heater、PD、System、Display 等项目。
- 输入：L=UP、R=DOWN、ENTR=ENTER、EXIT=BACK（低有效）；OLED RES# 与 MCU PF2/NRST 共网，外部 VCC，禁用内部电荷泵。
- **字体：唯一的 Fusion Pixel 12px Proportional Unicode 子集**，95 个 ASCII 字形 + 89 个汉字；英文和中文通过同一 U8g2 字库绘制。
- 国际化：应用菜单仍以英文原文作为键；`APP/i18n.c` 仅在绘制时翻译。语言开关位于 `Settings → System`，复位后恢复英文。
- TEXT 选中框：字符宽度使用 UTF-8 像素宽度测量，左右留白 1px；语言模式选择不同的垂直 1px 边界补偿。

[汉化与字库说明](I18N.md) · [固件架构](../../ARCHITECTURE.md) · [第三方声明](../../../THIRD_PARTY_NOTICES.md)
