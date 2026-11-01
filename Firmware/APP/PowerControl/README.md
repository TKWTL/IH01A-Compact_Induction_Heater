# PowerControl — IH01A STM32G071 采样验证阶段

**本阶段目标：** 验证 TIM6→ADC1→DMA1 CH2→SRAM ISR 的 8 通道采样，转换为 ADC 引脚电压，并在 Statistics → System 显示；MCU 温度使用出厂双点校准。**加热输出禁用。**

## 目录结构

| 文件 | 职责 | Keil |
|---|---|---|
| `conv_adc.c/h` | 8 Rank 双缓冲 DMA、Q4 采样滤波、ADC 原始数据快照、2.5 V mV 换算和 MCU 温度校准 | PowerControl |
| `conv_controller.c/h` | ADC/TIM6 启停、STOP/MONITOR/FAULT 状态、DMA HT/TC ISR、ISR_TIME PF1 脉冲、故障闭锁 | PowerControl |
| `dashboard.c/h` | Statistics → System 的 8 行静态 MiaoUI PAGE_TEXT，100 ms 原始码值更新 | PowerControl |
| `Legacy/*.legacy.*` | G474 HRTIM/PR/PID/三相逆变旧代码，仅参考，未加入 Keil | 不编译 |

原先的单体 `power_control.c/h` 已删除。暂未移植的 `conv_pwm`、`conv_loop`、`conv_protection` 在完成采样验证和硬件安全链后重新设计，**不会编译旧浮点实现**。

### 向量表与链接

**系统初始化职责放在 `Core/Src/main.c`**：在 `USER CODE BEGIN 0` 定义 `RelocateVectorTable()`，在 `SysInit` 复制 Flash `__Vectors` 至 SRAM，并将 `SCB->VTOR` 置为 SRAM 向量起始地址。PowerControl 不拥有向量表，也不改变 Cortex-M 启动过程。

Keil scatter 为 `MDK-ARM/Ld/Compact_Induction_Heater.sct`：
- Flash `0x08000000`，128 KiB，保留原始复位向量；
- 常规 RAM `0x20000000`，32 KiB，含 1 KiB heap、1 KiB stack；
- `.ih_ramfunc` 从 `0x20008000` 开始，最多 3584 B（启动时复制到 SRAM）；
- `.ih_vectors` 位于 `0x20008F00`，256B、256B 对齐；由 `main.c` 填充并设置 VTOR。

注意：未运行 Keil，必须核对 .map 中这两个区的真实位置、占用和加载行为。

### ADC Rank 物理对应（由 adc.c 和 CubeMX 确认）

| DMA 下标 | ADC Rank | 通道 | 目标 |
|---:|---:|---|---|
| 0 | 1 | PB10 / IN11 | CT_RMS |
| 1 | 2 | PA5 / IN5 | IBUS |
| 2 | 3 | PA1 / IN1 | NTC_POT |
| 3 | 4 | PB2 / IN10 | VBUS |
| 4 | 5 | PA2 / IN2 | NTC_SYS |
| 5 | 6 | PA4 / IN4 | VBIAS |
| 6 | 7 | PA0 / IN0 | ISR_LOAD |
| 7 | 8 | 内部温度通道 | MCU_TEMP |

ADC 12bit、标称 VREF+ = 2500mV、ADC1 扫描 8 Rank、DMA1 CH2 循环 16 halfword；HT 和 TC 各对应一整组 8 Rank。TIM6 ARR=3199，按 64MHz 时基理论约 20kHz；TIM1 专用时钟 128MHz、ARR=853，约 150kHz，完全解耦。中断以实际 DMA 完帧事件触发，非“每 PWM 周期调用”。

### 转换公式和校准

外部通道统一显示 **ADC 引脚电压**，没有把电阻分压、CT 灵敏度、NTC 传递函数或外部传感器偏置当作已知：

```text
V_ADC_mV = round(raw12 * 2500 / 4095)
```

MCU 温度使用 ST STM32G0 工厂 `TEMPSENSOR_CAL1_ADDR`、`TEMPSENSOR_CAL2_ADDR`（分别对应 30°C 和 130°C，**校准参考电压 3.0 V**）。先将当前 2.5V 参考下的 ADC 码折算到 3.0V：

```text
raw_at_3V = round(raw_at_2.5V * 2500 / 3000)
T_0.1C = 300 + (raw_at_3V - TS_CAL1) * 1000 / (TS_CAL2 - TS_CAL1)
```

全部用整数、无浮点。出厂值无效（0、0xFFFF 或两点相同）时显示 `MCU: invalid cal` 而不是编造温度。实际精度仍取决于 VREF+ 真实值、传感器采样稳定时间和器件规格。

### Statistics → System 显示示例

```text
[Return]
CT_RMS: 0.000V
IBUS: 0.000V
NTC_POT: 0.000V
VBUS: 0.000V
NTC_SYS: 0.000V
VBIAS: 0.000V
ISR_LOAD: 0.297V
MCU: 25.0C
```

这些数字**只是格式示例**，不是当前硬件实测。首次采样前显示 `Waiting for ADC`；FAULT 时显示 `ADC FAULT`。所有 8 个标签为静态 char 数组，定时最多 100 ms 更新一次，可使用 MiaoUI 的原有翻页/滚动；不分配堆，PD in 页面保留。

### ISR_TIME 与 ISR_LOAD

ISR 入口 PF1 高、出口低；已实测的高电平约 4.5µs、重复频率 20kHz，即占用率约 9%。若 RC 积分后连接到 PA0 (ISR_LOAD)，理想平均电压约 `3.3V × 9% = 0.297V`；12bit / 2.5V 对应约 487 LSB。超过 75.8% 占空率时 ISR_LOAD 会达到 ADC 满量程；不能将 4095 视为 100%。

本轮移植可能改变 ISR 执行时间，必须复测 PF1。状态机只用于采样与故障记录，**不会设置 TIM1 CCR/ARR、启动 PWM 或拉高 PWM_EN**。DMA TE、ADC OVR、HT 与 TC 同时积压时进入不可自动恢复的 FAULT。

## 实机验收

1. Keil Rebuild，确认 `PowerControl` 独立分组只含 3 个新源文件，旧 G474 文件全部不参与编译。
2. 检查 `SCB->VTOR=0x20008F00`，`DMA1_Channel2_3_IRQHandler` 位于 SRAM，堆栈正常，无链接内存重叠。
3. 确认 System 页面逐项与外部基准电压一致；特别检查 ISR_LOAD 与示波器 PF1 对应关系。
4. MCU 温度与环境/升温趋势大致一致；测试原始工厂校准字的合法性，不把无效读数显示为真实温度。
5. 反复切换菜单、休眠/唤醒及 USB-PD 插拔；查看 ADC FAULT、DMA HT/TC、负载时间，不允许 TIM1/PWM_EN 启动加热。

## CubeMX 重新生成后的 DMA 中断入口（Keil L6200E）

`Core/Src/stm32g0xx_it.c` 中 CubeMX 会生成 `DMA1_Channel2_3_IRQHandler()`，
而 `APP/PowerControl/conv_controller.c` 已提供用于 SRAM 执行的**强符号**实现。
两者都为强符号时链接报 `L6200E multiply defined`。现在在
`stm32g0xx_it.c` 的 `USER CODE BEGIN PFP` 放入
`__WEAK void DMA1_Channel2_3_IRQHandler(void);`，把 CubeMX 生成的
HAL 回调入口转为弱符号；链接器选择 PowerControl 中的强定义。
该声明在 CubeMX USER CODE 范围内，可在重新生成时保留。

上板需用 .map 文件确认 `DMA1_Channel2_3_IRQHandler` **最终仅解析到**
`conv_controller.o` 的 `.ih_ramfunc` SRAM 运行地址（约 `0x20008000`），
再核对 SRAM VTOR 中的 DMA1 CH2/3 槽位确实指向该入口。
不得同时将 `conv_controller.c` 排除编译，否则只会留下旧的弱 HAL 中断，
无法运行定点采样控制状态机。

## 2026-11-01 温度慢任务

沿用 Collar-Radio `Application/Algorithms/system_statistic.c` **低频应用层换算、与采样逻辑解耦**的思想，但 IH01A 保留自己的 20kHz TIM6→8 Rank ADC→DMA 和 Q4 IIR 结构；不在中断中执行查表/除法。

`APP/algorithm/ntc_lut.c/h`：SYS=10k/B3950 + 6.8k top + 20k parallel；POT=10k/B3950 + 1.2k top + 3.6k parallel。两张 39 点、每 5°C 一档的只读 ADC 码表，总计 **156B**；使用二分查找和 32 位整数线性插值，`-40.0..150.0°C`，结果 0.1°C。标称条件：3.3V 激励、2.5V ADC VREF、12 bit。

`PowerControl/temp_service.c/h`：在 `APP_Tasks_RunOnce()` 中每 **100ms / 10Hz** 读取一次已完成的 **filtered** ADC 快照，计算 NTC_SYS、NTC_POT、MCU 出厂双点校准温度并更新统一快照。无浮点、无堆分配。

`Statistics/System` 中两路 `NTC_*` 从电压改成带符号的 `xx.xC`，片上 `MCU` 也改从慢服务中读取，而不在页面内重复解算。ADC 尚未就绪、扫描停止、样本不再更新或某路码值超过该 NTC 标称表区间时，该路标记无效并显示 `FAULT`，不把极端码值钳到 -40/150°C 充当真实安全温度。独立 LUT 函数仍返回钳位值，同时以返回值标记其不在表范围。

**注意：**温度换算频率降到 10Hz，**不是将 NTC 或 MCU 从 20kHz 扫描序列中删除**，因此 DMA Rank、HT/TC 对齐和现有高频电流采样时序都未变化。要真正降低这三路 ADC 的采样频率，需重构扫描组或使用额外触发，不在此安全性敏感的改动中混做。保护滞回、开短路检测以及电阻容差校准尚需结合硬件实验验证；本阶段始终禁止加热 PWM。
