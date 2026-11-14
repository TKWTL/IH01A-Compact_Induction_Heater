# IH01A 研究与实验档案

**项目定位：实验性小型感应加热器；有效加热功率未达到原定目标，当前作为技术储备保留。** 本目录收录可复核的测量数据、波形、受限模型及失败案例，不以软件功能实现清单替代实验证据。

## 实验记录

| 研究主题 | 证据与边界 |
|---|---|
| [12 V 空载扫频与移相](Experiments/SWEEP_12V_UNLOADED_REPORT.md) | 160–400 kHz、10–180°、450 个点；测得空载 CT_RMS 最大 1.687 A。空载电流不是加热输出功率 |
| [低功率加热与零电流检测](Experiments/LOW_POWER_ZCD_BENCH.md) | TP1941 → TIM1_CH2 捕获；相位计算、误差来源与安全停止条件 |
| [ZCD 相位补偿研究](Experiments/ZCD_PHASE_COMPENSATION.md) | 历史空载拟合；绕组与 CT 改动后模型不再具备自动控制资格 |
| [双周期脉冲检锅研究](Experiments/PAN_DETECTION_PULSE_RINGDOWN_STUDY_2026-11-10.md) | 标称 12 V、160 kHz、2 周期；六组捕获与四种示波器波形，**不能据此宣称可靠检锅** |
| [功率级保护接入与故障](Experiments/POWERCONTROL_PROTECTION_BRINGUP.md) | ADC AWD、DAC/COMP、TIM1 BRK 及调试故障边界 |
| [Heater 手动实验设置](../Firmware/APP/PowerControl/HEATER_SETTINGS_BENCH.md) | 频率、移相、CT_RMS 及短脉冲检锅操作记录；非产品用户手册 |

## USB-PD 实验

- [PD 实验索引](PD/README.md)
- [28 V/5 A EPR 协商证据与局限](PD/EPR_28V_VALIDATION.md)
- [C140 适配器与复位比较](PD/PD_C140_COMPARISON.md)
- [PD 串口诊断记录](PD/PD_UART_DIAG.md)

## 证据规范

- 原始示波器截图及资料见 [Images/Testing](../Images/Testing)；实验报告的图表链接指向该目录。
- 区分 **PD 协商电压/电流额度**、**电气输入功率** 与 **工件实际吸收的有效热功率**；三者不能混写。
- 单点波形不证明连续运行可靠性、ZVS/ZCS、效率或温升性能。尚无可用的整机效率/加热量闭环验收数据。
- 与代码结构有关的说明就近存放： [固件架构](../Firmware/ARCHITECTURE.md)、[Framework](../Firmware/APP/framework/README.md)、[功率控制](../Firmware/APP/PowerControl/README.md)、[CLI](../Firmware/BSP/Console/CLI.md)、[汉化](../Firmware/APP/MiaoUI/I18N.md)。
