# IH01A · Compact Induction Heater

STM32G071CBU6 感应加热器固件/硬件仓库（当前仅完成基础框架移植，非可交付加热控制固件）。

## 固件基础（2026-10-31）

- **MCU**：STM32G071CBU6，64MHz，128KiB Flash，36KiB RAM。
- **GUI**：DPC27A 定点整数 MiaoUI + 裁剪 u8g2，SSD1315 128×64，4-wire SPI1，VCC 外部供电、禁用电荷泵。
- **按键**：L（PA12）、R（PA11）、ENTR（PC7）、EXIT（PD3），均上拉、低有效。
- **协作调度**：coroOS；主循环始终先调用原有 `USBPD_DPM_Run()`。
- **PM**：来自 PB02B/DPC27A 的冻结式空闲倒计时，最大等级 **UI_OFF**。
- **安全**：此版本不会通过 GUI 使能加热 PWM；加热控制算法与保护联动尚未实现。

项目代码、引脚、架构和移植差异见 [Firmware/README.md](Firmware/README.md)、
[Docs/FIRMWARE_ARCHITECTURE.md](Docs/FIRMWARE_ARCHITECTURE.md) 与
[Docs/PORTING_NOTES.md](Docs/PORTING_NOTES.md)。

**当前状态：** 已接入 Keil 工程源文件，尚未完成 ARMClang 构建与上板测试。
不得在未经 PWM/比较器/USB-PD 安全验证前将此版本用于实际加热负载。

许可：本仓库根目录 `LICENSE` 及 `THIRD_PARTY_NOTICES.md`。
