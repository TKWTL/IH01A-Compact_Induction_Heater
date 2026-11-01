# IH01A PowerControl 移植基线（STM32G071）

> 当前仅接通 ADC 采样、DMA 中断和定点状态机。**PWM_EN 与 TIM1 MOE 始终被关闭；不支持带功率运行。** 本工程不能直接使用从 G474 AC-AC 变换项目复制过来的 HRTIM/浮点控制代码。

## 本次实际编译的文件

- `power_control.h/.c`：IH01A 专用移植层；DMA 双帧、TIM6 ADC 触发、Q4 IIR 滤波、Q15 命令模型、故障闭锁、ISR_TIME 脉宽标记、SRAM 中断向量表重定位。
- 其余旧版 `conv_*.c/h`、`algorithm_*.c/h`、`dashboard.c/h`：**参考代码，未进入 Keil 编译**。保留作为后续对照，不能直接调用旧版的 HRTIM、float 或 PR/PID 模块。
- `stm32g4xx_it.c`、`stm32g474xx_flash.sct` 和相关 Markdown：原 G474 移植参考，不是 IH01A 的启动/中断/链接文件。
- 真正生效的中断：`power_control.c` 中的 `DMA1_Channel2_3_IRQHandler`；
  真正生效的 scatter：`Firmware/MDK-ARM/Ld/Compact_Induction_Heater.sct`。

## 实际采样链路

```text
TIM6 update TRGO (独立时基，标称 20 kHz)
  -> ADC1 外部上升沿触发扫描 Rank 1...8
  -> DMA1 Channel2 circular 16 x uint16
  -> HT (第一组 8 路) / TC (第二组 8 路)
  -> SRAM 中的 DMA1_Channel2_3_IRQHandler
     ISR_TIME PF1 = HIGH
     DMA 错误/ADC overrun/两帧积压检查
     PowerControl_DmaFrameISR()  [Q4滤波+Q15状态机]
     ISR_TIME PF1 = LOW
```

**实际顺序**（来源：`Core/Src/adc.c`、`.ioc`）：

| Rank | ADC通道 | 引脚 | 信号名称 | 用途 |
|---|---|---|---|---|
| 1 | IN11 | PB10 | CT_RMS | CT RMS 采样 |
| 2 | IN5 | PA5 | IBUS | 母线电流 |
| 3 | IN1 | PA1 | NTC_POT | 加热盘 NTC |
| 4 | IN10 | PB2 | VBUS | 输入母线电压 |
| 5 | IN2 | PA2 | NTC_SYS | 系统 NTC |
| 6 | IN4 | PA4 | VBIAS | 偏置电压 |
| 7 | IN0 | PA0 | ISR_LOAD | ISR 占空率模拟量 |
| 8 | TEMP | 内部 | MCU_TEMP | 内部温度通道 |

TIM6 与 TIM1 **独立**：TIM1 的 ARR 当前为 853，专用时钟 128 MHz，标称约 150 kHz；TIM6 ARR 为 3199，APB1 定时器时钟标称 64 MHz，更新事件约 20 kHz。控制 ISR 每完成一帧执行一次，并不假设“一个控制 ISR 等于一个 PWM 周期”。

## 2.5 V 标称基准与 ISR_LOAD 饱和

软件采用 `PC_ADC_REFERENCE_MV=2500`（标称 VREF+）、12 位 ADC（0..4095），引脚电压换算为：

```text
Vpin_mV = round(ADC_raw * 2500 / 4095)
```

目前只提供 **ADC 引脚电压**和原始/滤波代码。外部传感器增益、偏移、NTC 查表、MCU 温度校正尚未移植，不能把 ADC 引脚的 2.5 V 当成母线的实际 2.5 V。应在硬件上核实 VREF+ 确实为 2.5 V，并确认 VREFBUF/外部参考的供电路径。

`ISR_TIME` 的高电平为 VCC3.3V；如果 `ISR_LOAD` 是对该脉冲的 RC 积分，则其 ADC 最大可测占空比：

```text
Dmax = 2.5 / 3.3 ~= 0.758 = 75.8%
```

从约 76% 之后会在 4095 附近饱和；`PowerControl_IsrLoadPermille()` **仅返回可测范围内的近似占空千分比**，没有推断被削顶后的真实占空率。还需确认板上 RC 时间常数、负载与实际 VCC/VREF。

## 定点与状态机

- `STOP → MONITOR → FAULT`。启动校准 ADC，启用 DMA 和 TIM6 后进入 MONITOR；DMA 错误、帧积压、ADC 溢出或初始化失败进入 FAULT，**不自动恢复**。
- 采样滤波：原始码值 Q4，`filtered += (input - filtered) >> 3`，无需浮点。
- 调制目标：Q15（0..32767），软件缓升速度默认为每 ADC 帧 32（与 TIM1 PWM 周期无关，尚需按实测采样率与功率级响应调整）。
- `PC_MOD_PHASE`、`PC_MOD_FREQUENCY`、`PC_MOD_BURST` 分别输出独立的**软件请求量**。间歇模式采用 256 个 ADC 帧的软件观察窗；没有触发任何 TIM1 比较值/开关沿或桥臂使能。
- 这些都是待验证的控制状态机骨架，**不是实际加热功率闭环**。尤其是间歇使能的物理起止沿必须最终与 PWM 安全同步，不能直接从 TIM6 ISR 开断桥臂。

## SRAM 执行区和中断向量表

`Firmware/MDK-ARM/Ld/Compact_Induction_Heater.sct`：

| 区 | 地址 | 空间 |
|---|---|---|
| FLASH 复位向量+RO | 0x08000000 | 128 KiB |
| 常规 RW/ZI（含 1 KiB 堆、1 KiB 栈） | 0x20000000 | 32 KiB |
| `.ih_ramfunc`（控制 DMA ISR/ISR 状态机） | 0x20008000 | 3584 B 上限 |
| 保留未使用缓冲 | 0x20008E00 | 256 B |
| `.ih_vectors`（中断向量表副本） | 0x20008F00 | 256 B |
 
上电 C 启动例程负责将 RAM 代码段从 Flash 装载到 SRAM；`PowerControl_RelocateVectors()` 在 `main()` 的 `SysInit` 处将 Flash `__Vectors` 拷贝到 SRAM 并写 VTOR，随后才初始化 DMA 和外设。FLASH 启动向量保持不变。**必须使用 Keil .map 文件核对 .ih_ramfunc 实际执行地址和容量；未运行目标编译，链接可用性仍待验证。**

## 明确禁止事项

- 不用 G474 的浮点 PR/PID、HRTIM 寄存器和 0x10000000 CCM 地址。
- 未确认桥臂接线、TIM1 CH1/CH3 组合模式、COMP→Break、死区与 PWM_EN 的有效极性之前，不实现 TIM1 输出控制。
- 不让 UI 中断或 USBPD 回调执行 ADC 控制逻辑；UI 只能通过 `PowerControl_GetSnapshot()` 读取稳定快照。
- 不假定 ISR_LOAD 达到 4095 表示 100% CPU 负载。
