# Third-party Notices（第三方组件声明）

本仓库自身代码的默认许可见根目录 `LICENSE`（GNU AGPL-3.0-only）。
引入的第三方源码保留原版权、许可证与使用限制；不得以仓库许可证覆盖上游许可证。

## MiaoUI（MIT）
- 作者：JFeng-Z，来源：DPC27A 定点整数移植。
- 文件：`Firmware/APP/MiaoUI/`；许可证：`Firmware/APP/MiaoUI/LICENSE`。
- IH01A 修改：128x64 参数、四按键映射、SSD1315 显示适配。
- `images/image.c/.h` 图标素材含单独的 GPL-3.0-or-later 来源声明，应保留其文件头和相应再分发义务。

## u8g2 裁剪版（BSD-2-Clause）
- 原项目：https://github.com/olikraus/u8g2，作者 Oliver Kraus。
- 文件：`Firmware/ThirdParty/u8g2/`；许可证：`Firmware/ThirdParty/u8g2/LICENSE`。
- IH01A 修改：SSD1315 128x64 驱动、1024B 全屏帧缓冲、外部 VCC 供电初始化。

## coroOS / PM
- 来源：TKWTL/DPC27A-Desktop_PD_Power_Converter 与 TKWTL/PB02B-Pocket_Powerbank。
- coroOS 基于无栈 Protothreads 风格的项目实现；若另行分发为通用库，应复核原始实现的版权与许可。
- PM 保留不依赖具体 MCU 的核心逻辑，IH01A 只配置 UI_OFF。

## STM32 HAL/LL、USB-PD
- 来自原有 STM32CubeMX/Keil 工程；遵循各对应文件中 ST 的版权与许可说明。
- 本次未重写、替换或扩大原始 USB-PD 协议栈授权范围。
