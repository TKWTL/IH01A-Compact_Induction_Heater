# IH01A 固件架构与功能边界

目标芯片 STM32G071CBU6，Arm Cortex-M0+；STM32CubeMX + Keil ArmClang 6。本文是架构索引，具体实现以同目录源代码为准，不再混入早期阶段性的“下一步”交接记录。

## 主链路

```text
main(): 初始化 CubeMX 外设 + 应用服务
  -> ST USBPD_DPM_Run()                   (非 RTOS 主循环)
      -> USBPD_DPM_UserExecute()
          -> APP_Tasks_RunOnce()
              -> coroOS: 按键 / PM / UI / Heater
              -> SSD1315 DisplayService (SPI1 DMA)
```

- `APP/algorithm/`：PD Sink PDO 选择、EPR 策略、诊断及 Source PDO 只读缓存。
- `APP/PowerControl/`：TIM1 移相驱动、TIM6/ADC/DMA 采样、CT/ZCD、保护故障、加热状态、检锅短脉冲。
- `APP/MiaoUI/`：菜单、双语原文键查找、Fusion Pixel 单套 UTF-8 字体、参数控件。
- `APP/framework/`：RTC/Uptime、PM、蜂鸣器、系统复位。
- `BSP/`：SSD1315、按键、串口；`USBPD/`：ST FULL 协议栈与接口。
- `MDK-ARM/`：Keil 工程、启动、SRAM 热代码链接及中断向量安排。

## 重要工程限制

1. USB-PD 状态机持续运行；不要把应用任务放在不会返回的 `USBPD_DPM_Run()` 之后。
2. ADC DMA 与相关功率控制 ISR 有 RAM 执行区；任何链接脚本调整必须复核 VTOR、入口地址、最坏延迟。
3. SSD1315 使用 SPI1 DMA；界面任务不得在协议关键时段引入不可控阻塞。
4. 功率级的 PC6 驱动使能、TIM1 BRK、COMP 与 ADC AWD 均属于安全链路，不允许以精简代码为由直接旁路。
5. **当前项目不具备经验证的实用加热功率、效率与热管理能力；不作为成品使用。** 历史数据和失败案例见 [实验档案](../Docs/README.md)。

源码就近文档：[Framework](APP/framework/README.md)、[PowerControl](APP/PowerControl/README.md)、[Heater CLI](BSP/Console/CLI.md)、[汉化](APP/MiaoUI/I18N.md)；PD 协商实验另见 [Docs/PD](../Docs/PD/README.md)。
