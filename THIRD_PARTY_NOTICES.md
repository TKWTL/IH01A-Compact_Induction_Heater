# Third-party Notices（第三方组件声明）

本仓库自身代码的默认许可见根目录 `LICENSE`（GNU AGPL-3.0-only）。引入的第三方源码、字体、图标应遵守各自版权及许可证，不因纳入本仓库而被重新许可为 AGPL。

## Fusion Pixel Font / 缝合像素字体（SIL Open Font License 1.1）

- 上游项目：[TakWolf/fusion-pixel-font](https://github.com/TakWolf/fusion-pixel-font)；作者及字体版权归原上游权利人。
- 使用字体：**Fusion Pixel 12px Proportional，简体中文（zh_Hans）**，取自 `fusion-pixel-12px-proportional-zh_hans.bdf`。
- 衍生资源：`Firmware/APP/MiaoUI/fonts/font_fusion_pixel_12_zh.c`。由原 BDF 裁剪、转换为 U8g2 Unicode/RLE 数据；当前包含 95 个 ASCII 字形及 89 个汉字，**英文和中文界面共同使用同一字库**。
- 许可证副本：`Firmware/APP/MiaoUI/fonts/LICENSE-OFL`（SIL OFL 1.1）。字体的版权与许可条款继续适用；不能把该字体数据视为本项目自创或仅受 AGPL 约束。
- 字形子集、编码和打包为 IH01A 项目适配；源 BDF 不在本仓库内，未来重新裁剪字体时须继续保留授权与出处声明。

## MiaoUI（MIT）

- 作者：JFeng-Z，来源：DPC27A 定点整数移植。
- 文件：`Firmware/APP/MiaoUI/`；许可证：`Firmware/APP/MiaoUI/LICENSE`。
- IH01A 修改：128×64 页面、四按键映射、SSD1315 显示、UTF-8、双语菜单和像素级光标布局。
- `images/image.c/.h` 图标素材含单独的 GPL-3.0-or-later 来源声明，应保留其文件头和相应再分发义务。

## U8g2 裁剪版（BSD-2-Clause）

- 原项目：https://github.com/olikraus/u8g2，作者 Oliver Kraus。
- 文件：`Firmware/ThirdParty/u8g2/`；许可证：`Firmware/ThirdParty/u8g2/LICENSE`。
- IH01A 适配：SSD1315 128×64、全屏帧缓冲、外部 VCC 初始化和 UTF-8 字形绘制。

## coroOS / PM

- 来源：TKWTL/DPC27A-Desktop_PD_Power_Converter 与 TKWTL/PB02B-Pocket_Powerbank。
- coroOS 基于无栈 Protothreads 风格的项目实现；若另行分发为通用库，应复核原始实现的版权与许可。
- PM 核心不依赖具体 MCU，IH01A 只使用 UI_OFF 休眠等级。

## STM32 HAL/LL、USB-PD

- 来自 ST 的 STM32CubeMX/Keil 工程及 USB-PD 中间件；遵循各对应文件和静态库的 ST 版权、再分发与许可要求。
- IH01A 的移植或故障修复不扩大上游协议栈授权范围。
