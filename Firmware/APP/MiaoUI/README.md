# IH01A MiaoUI

Root menu contains exactly four entries: Main / Settings / Data / About
(主界面／设置／数据／关于). Main/Data are non-operational placeholders;
Settings retains Sleep and Display options, About is a text page.

Ported from DPC27A integer-Q10 MiaoUI (MIT), SSD1315 128x64/u8g2.
Keys: L=UP, R=DOWN, ENTR=ENTER, EXIT=BACK (active-low).
OLED RES# shares MCU PF2/NRST by default. No reset GPIO is allocated.
External VCC must be sequenced safely; charge pump is disabled (8D 10).
Chinese glyphs and a general popup manager are not yet implemented.
