# IH01A Firmware · STM32G071CBU6

**状态：已完成面板、USB-PD EPR 和若干功率级实验；未达到目标有效加热功率，当前作为研发技术储备归档。** 本工程不是可以直接投入无人看守使用的电磁炉固件。

## 代码组织

- `Core/`、`Drivers/`、`MDK-ARM/`：CubeMX 生成层、HAL/LL、ArmClang 6 Keil 工程、链接布局。
- `USBPD/`、`Middlewares/`：ST USB-PD FULL Sink 中间件（修改受上游许可证约束）。
- `APP/algorithm/`：PDO/RDO、SPR/EPR 选档策略和 PD 诊断。
- `APP/PowerControl/`：TIM1 移相、ADC DMA 采样、CT/ZCD、保护和手动 Heater 控制。
- `APP/MiaoUI/`：SSD1315 OLED 菜单、Fusion Pixel UTF-8 单套字库、中英双语译表。
- `APP/framework/`：系统、RTC/Uptime、PM、蜂鸣器；`BSP/`：按键、显示及调试串口。
- `Tests/host/`：可选主机侧单元测试，不等价于实机功率安全验证。

## 关键文档

[当前架构](ARCHITECTURE.md) · [PowerControl](APP/PowerControl/README.md) · [Heater CLI](BSP/Console/CLI.md) · [Framework](APP/framework/README.md) · [MiaoUI 汉化](APP/MiaoUI/I18N.md)

原始实验、波形、PD EPR 协商证据统一见 [Docs](../Docs/README.md)。不再将某一中间开发阶段的「待实现」叙述当作当前版本状态。

## 实机验证边界

固件具备手动调频/移相及保护相关实现，但功率级闭环稳态、安全容错、热效率、检锅可靠性均未完成全工况验证。**不得据 28 V/5 A PD 合同声称可向负载稳定输出 140 W。** 改动 TIM1/COMP/ADC AWD/PC6 保护链路后需重新测量，不以主机单测通过取代示波器和故障注入试验。
