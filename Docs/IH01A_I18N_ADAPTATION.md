# IH01A MiaoUI 中英双语适配记录（2026-11-10）

## 范围

仅修改界面文本、字体、布局字宽计算及语言设置；不更改加热、检锅、PD 协议、ADC 或 16 A 过流保护。检锅研究见 [PAN_DETECTION_PULSE_RINGDOWN_STUDY_2026-11-10.md](./PAN_DETECTION_PULSE_RINGDOWN_STUDY_2026-11-10.md)。

## 实现方式

- **原文为键**：`AddItem`、`AddPage` 和 `ui_data_t.name` 保存英文原文，不对菜单内部标识、回调或结构进行中文替换。`Firmware/APP/i18n.c` 中的 `IH_I18n_Text()` 负责显示层精确匹配翻译；无匹配时保留原文。译文保留原文的**前导空格、`-` 以及成对方括号**（例如 `-Settings` → `-设置`、`[Return]` → `[返回]`），维护菜单层级和视觉缩进。
- **语言切换**：`Settings → System → Language` 是 MiaoUI 原生开关。英文 `OFF`、简体中文 `ON`；启动默认英文。切换即时生效，目前为 **RAM 状态**，复位后恢复英文，不涉及未完成的 EEPROM 异步写入。
- **字体**：简体中文采用 TakWolf [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) 12px Proportional zh_Hans，源自本次提供的 `fusion-pixel-12px-proportional-zh_hans.bdf`。编译内置的是 Unicode/U8g2 RLE 子集，含 95 个 ASCII 字形和 89 个汉字，字库占 3428 字节；英文模式继续使用原 `font_menu_main_h12w6`。
- **宽度**：菜单光标、滚动边界与图标标题改用 `Disp_GetUTF8Width(IH_I18n_Text(...))` 测量，不能继续以 `strlen() × 6` 处理中文。绘制调用现有的 `Disp_DrawStr()` / `Disp_DrawUTF8()`，底层均为 `u8g2_DrawUTF8()`。TEXT 页文本从 X=1 开始，选中框从 X=0 开始，宽度为 `min(实际 UTF-8 字宽+2px, 文本区可用宽度)`，左右各留 1px；上沿比原实现高 1px，高度从 11px 增至 12px，下沿不变。文本区域被裁剪时，末尾右侧留白可能不足 1px。
- **文本控件**：关于页按 UTF-8 码点分段、按实际像素宽度折行，避免把一个三字节汉字当三个字符绘制。
- **Keil**：`Compact_Induction_Heater.uvprojx` 纳入 `APP/i18n.c` 和 `APP/MiaoUI/fonts/font_fusion_pixel_12_zh.c`。

## 字库更新与版权

字体归属 TakWolf；遵守 [SIL Open Font License 1.1](../Firmware/APP/MiaoUI/fonts/LICENSE-OFL)。当前 89 个汉字的字符清单在 [Tools/ih01a_zh_charset.txt](../Tools/ih01a_zh_charset.txt)。

若新增译文，先更新 `i18n.c`，收集新增汉字，使用上游 u8g2 的 `bdfconv` 根据 BDF 重新生成字体。例如：

```bash
bdfconv -f 1 -b 0 -m "32-126" -u Tools/ih01a_zh_charset.txt \
  -n font_fusion_pixel_12_zh \
  -o Firmware/APP/MiaoUI/fonts/font_fusion_pixel_12_zh.c \
  fusion-pixel-12px-proportional-zh_hans.bdf
```

源 BDF 文件未加入仓库（约 5 MB），并不影响使用内置子集编译。每次扩展字库后应核对所需字符覆盖率、Flash 容量、字体尺寸及 `U8G2_WITH_UNICODE` 开关。

## 验证情况与后续

已完成静态检查：翻译所需汉字均可在子集字库中查到；字形二进制按 U8g2 RLE 解码与原 BDF 像素匹配；字体数据 CRC32 为 `85B981D2`；Keil 项目登记无重复；固定字宽计算已替换。

**尚未**运行 Keil/armclang 实际构建、SSD1315 实机显示、键盘操作与 Flash 占用测量，不能宣称通过硬件回归。请实机检查中文切换后菜单光标、高亮、滚动、关于页换行、英文切回和复位后的语言默认值。

当前翻译覆盖静态菜单、休眠选项、控制标签及关于页；实时数值、故障码、PD Source PDO、串口 CLI 日志保持英文/原格式，避免改动工程诊断字段。这些动态文案可在后续独立审核后按相同方式扩展。

为了避免额外 CI 邮件，此次不增加 GitHub Actions 自动触发；已有 host-tests workflow 保留手动运行策略。
