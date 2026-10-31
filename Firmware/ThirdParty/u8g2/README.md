# Trimmed u8g2 BSD-2-Clause graphics subset

Ported from DPC27A to SSD1315 128x64. 1024-byte full framebuffer. External VCC: 8D 10 charge pump disabled. See LICENSE.

## IH01A UTF-8 / Unicode BMP

`U8G2_WITH_UNICODE` is enabled in `u8g2.h`; existing
`u8x8_utf8_next` and `u8g2_DrawUTF8/u8g2_GetUTF8Width`
now have 16-bit Unicode glyph lookup available. The MiaoUI `Disp_DrawStr`
wrapper treats strings as UTF-8, and the explicit `Disp_DrawUTF8` path
is retained.

**This is only an i18n infrastructure change.** Current
`font_menu_main_h12w6` contains ASCII glyphs, not Chinese characters.
To draw Chinese, provide a Unicode-enabled u8g2 font containing the needed
U+4E00... glyphs, keep a supported character subset, and select that font.
4-byte UTF-8 code points outside BMP are not supported by the 16-bit path.
