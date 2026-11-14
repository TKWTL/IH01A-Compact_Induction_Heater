# IH01A PowerControl

STM32G071 的谐振加热实验控制层。功能包括手动 TIM1 调频/移相、ADC DMA 数据采集、CT/ZCD 相位观测、COMP/TIM1 Break 和 ADC AWD 保护、加热状态门控，以及短脉冲检锅研究。**该原型未达到有效加热功率目标，不能视作可用或安全认证的加热控制器。**

## 模块定位

- `conv_adc.c`：TIM6 TRGO、ADC1 8 Rank、DMA1 CH2，2.5 V 参考下的采样与定点换算。
- `conv_controller.c`、`conv_phase.c`：TIM1 调频移相、DMA ISR、检锅脉冲及快速状态。
- `conv_protection.c`：ADC AWD、DAC/COMP、TIM1 Break 和故障闭锁；**不能因压缩固件而绕过**。
- `conv_current.c`、`conv_zcd.c`、`conv_auto_freq.c`：CT_RMS、零电流相位及待重新标定的自动追频。
- `heater_control.c`、`heater_budget.c`：启停状态与输入预算；预算不等于已实现的闭环功率限制。
- `heater_ui.c`、`dashboard.c`：设置与状态显示；`pan_probe_config.h`：检锅参数。
- `Legacy/`：原 STM32G474 的参考工程，不参与当前编译。

## 8 Rank ADC 顺序

| DMA 索引 | ADC 输入 | 测量对象 |
|---:|---|---|
| 0 | PB10 / IN11 | CT_RMS |
| 1 | PA5 / IN5 | IBUS |
| 2 | PA1 / IN1 | NTC_POT |
| 3 | PB2 / IN10 | VBUS |
| 4 | PA2 / IN2 | NTC_SYS |
| 5 | PA4 / IN4 | VBIAS |
| 6 | PA0 / IN0 | ISR_LOAD |
| 7 | MCU 内部温度通道 | MCU_TEMP |

定点输入电压估算：`ADC_mV = round(raw12 × 2500 / 4095)`；VBUS 原理分压标称 1/16。需以实测 VREF 与传感器校准值为准。DMA/保护 ISR 的 RAM 链接地址与向量表安排见 [Firmware 架构](../../ARCHITECTURE.md)。

[Heater 原型框架](HEATER_FRAMEWORK.md) · [实验设置](HEATER_SETTINGS_BENCH.md) · [UART CLI](../../BSP/Console/CLI.md) · [实际实验数据](../../../Docs/README.md)
