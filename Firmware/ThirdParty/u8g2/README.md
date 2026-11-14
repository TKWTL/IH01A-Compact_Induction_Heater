# Trimmed U8g2（BSD-2-Clause）

从 DPC27A 移植的 SSD1315 128×64 绘制子集，1024 B 帧缓冲；外部 VCC 供电，禁用内部电荷泵（`8D 10`）。上游版权与许可证见 `LICENSE`。

## UTF-8 / Unicode

`U8G2_WITH_UNICODE` 已启用。MiaoUI 的 `Disp_DrawStr` 与 `Disp_DrawUTF8` 使用 `u8g2_DrawUTF8()` 绘制 UTF-8 文本，`Disp_GetUTF8Width` 用于像素宽度计算。

**当前已实际提供中文字体。** `APP/MiaoUI/fonts/font_fusion_pixel_12_zh.c` 包含 Fusion Pixel 12px Proportional 字符子集（95 ASCII + 89 汉字），中英文共用一套字体。UTF-8 四字节字符超出当前 U8g2 16 位 BMP 字形处理路径的支持范围。后续新增中文译文时应重新生成子集并核对字符覆盖。

字体许可另见仓库根目录 [THIRD_PARTY_NOTICES.md](../../../THIRD_PARTY_NOTICES.md)。
