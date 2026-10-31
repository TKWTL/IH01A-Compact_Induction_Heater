# IH01A · Compact Induction Heater

STM32G071CBU6 感应加热器固件/硬件项目。当前已完成**基础控制面板联调**，尚未实现可投入使用的加热控制与保护闭环。

## 当前实机状态（2026-10-31）

- 用户已在实机确认：OLED 正常显示、四键操作、UI_OFF 休眠与唤醒正常。
- OLED：SSD1315 128×64，SPI1，外部 VCC、禁用内部电荷泵；RES# 与 STM32 PF2/NRST 共网。用户已修正显示方向，现使用 `U8G2_R2`。
- 固件：ST USB-PD 非 RTOS 主循环中，由 `USBPD_DPM_UserExecute()` 调用 coroOS；PM 深度暂限 `UI_OFF`。
- CubeMX / Keil：用户实测堆空间为 0 时程序卡住；**当前可运行配置为 0x400（1024 B）堆、0x400（1024 B）栈**。这不证明 1 KiB 是长期负载下的充分值，也不证明分配发生在 USB-PD 内。
- 功能边界：Main / Settings / Data / About 四项菜单；Main/Data 尚为占位。加热 PWM 尚未接入 UI，不能用于实际加热负载。

> 屏幕显示、按键、UI_OFF 的成功为用户报告的实机结果；PD 多档协商与插拔压力测试、内存峰值、OLED/PD 实时性、过流/过温硬件保护等**尚未完成独立验收**。

下一阶段优先事项与验收标准：**[Docs/NEXT_STAGE_PLAN.md](Docs/NEXT_STAGE_PLAN.md)**。

更多信息：[固件说明](Firmware/README.md) · [架构](Docs/FIRMWARE_ARCHITECTURE.md) · [测试记录](Docs/TEST_REPORT.md) · [移植说明](Docs/PORTING_NOTES.md)。

许可：根目录 `LICENSE` 和 `THIRD_PARTY_NOTICES.md`。
