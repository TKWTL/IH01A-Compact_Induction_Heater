# IH01A · PowerControl ADC / DAC / COMP 保护接入（2026-11-02）

## 1. 硬件与时钟

- MCU：STM32G071，SYSCLK 64MHz；TIM1 独立 PLL 时钟 **128MHz**。
- TIM1 新初值 `PSC=0, ARR=426`：`128MHz/(426+1) = 299.77kHz`。对 30µH + 37.6nF 谐振腔，理论 `f0≈149.86kHz`。**这只是默认频率，不代表已允许加热。**
- ADC：12-bit，VREFBUF 2.5V，`ADC_count = round(V_ADC / 2500mV × 4095)`；TIM6 约 20kHz 触发 8-rank ADC1 DMA 循环扫描。
- CubeMX 的 TIM1_CH2 捕获 DMA（DMA1_Channel3）保留，但不在本阶段启动使用；ADC DMA1_Channel2 仍由现有 RAM ISR 处理。
- COMP1/2 窗口合成输出选择 COMP1，经 **TIM1 BRK，低电平有效**。PA3 为 COMP2 正输入，窗口模式共用此模拟输入。DAC1_CH1/CH2 分别接两个 COMP 负输入。

## 2. 三个 ADC Analog Watchdog 阈值

| AWD | 监测量 | 条件及输入关系 | 使用的配置值 | 说明 |
|---|---|---|---|---|
| 1 | VBUS / ADC1 IN10 | `V_BUS × 1/16`, 上限 33V | HIGH **3379**；LOW 0 | AWD1 12-bit，CubeMX 原值已经正确 |
| 2 | IBUS / ADC1 IN5 | `V_in = V_zero + I×0.2134V/A` | 先 DISABLE 中断，校准后动态设置 | AWD2 只有有效 8-bit 比较精度 |
| 3 | NTC_SYS / ADC1 IN2 | LUT 在 90°C 对应 **627** 码；温度升高码值下降 | LOW **640**；HIGH 4095 | AWD3 8-bit，选择 640 使原始 ADC <=639 即提前触发，约略低于 90°C |

VBUS 计算：`V_ADC=33000/16=2062.5mV`；
`2062.5×4095/2500=3378.375`，配置 3379，超过上阈值后报警。

IBUS 的增益换算：
`ΔADC = round(I_mA×2134×4095/25,000,000)`。
1A ≈350 counts，5A ≈1748 counts，7A ≈2447 counts。
AWD2 限值在成功零点校准后从 `ibus_zero_raw ± ΔADC` 计算，
8-bit 硬件比较所需的低阈值上取整到 16 码，高阈值下取整到
16 码。零点 1900 的示例，5A → 低阈值 160、高阈值 3648。
`thread_heater` 每 100ms 用 **有效 PD 合同的电流**或
**DCin 7A**更新 AWD2；没有有效输入或零点未完成时禁用。

**可测范围警告**：若 IBUS 零点约 1900，+7A 预测码值
`1900+2447=4347>4095`，这不是一个完整可测的 7A 窗口。
软件会在接近 ADC 上/下轨时提前停机，但无法利用 AWD2 在
7A 物理阈值处精确报警。必须结合样机的真实零点与电流传感器
实际动态范围作最终限额，不能把 PD 电流协商额定值等同于可测电流。

## 3. COMP ±12A 与 DAC1

CT_OCP 电流检测增益 **0.100V/A**，正负 12A 对应
`±1.200V`，相对 VBIAS 的 12-bit DAC 码跨度：
`round(1200mV×4095/2500mV)=1966`。

- `DAC1_CH1=VBIAS_raw−1966`（低阈值）。
- `DAC1_CH2=VBIAS_raw+1966`（高阈值）。
- 例如 VBIAS=2048（1.25V），DAC1_CH1=82，DAC1_CH2=4014。
- **ARR 不参与这个阈值**：这一路模拟前端 0.1V/A 固定，不随频率变化。
- VBIAS 必须满足 `1966≤VBIAS_raw≤2129`；超过范围不允许
  对 DAC 值剪裁成一个不可靠的 ±12A 窗口，而是锁存故障。

上电零点校准（256 帧等待 + 2048 帧平均）完成、VBIAS 有效时，
前台首先保持 `PWM_EN=LOW` 和 `TIM1.MOE=0`，配置 DAC1
两个通道并启动，再复设 COMP1 公共窗口模式并启动 COMP1/2。
原因是当前代码中 `MX_COMP2_Init()` 可能在 COMP1 之后重写
共享的 WINMODE 配置。校准期间绝对禁止加热。启动后若 VBIAS
漂移，仅由前台修改 DAC 阈值；更新异常或范围超限会停机锁存。

HAL COMP 注释说明：合成窗口输出在测量电压处于上下阈值之间时
为 **HIGH**，越过任一端则为 **LOW**；TIM1 BRK 采用低有效输入。
只有示波器和故障注入实测证实此链路确实可以封锁 TIM1 MOE，
才可进一步开放加热运行。本轮 **Heater_GetIsrTargetMw() 仍固定为零**。

## 4. 中断、状态机及通知

`ADC1_COMP_IRQHandler` 在 `HAL_ADC_IRQHandler` 之前执行
`ConvProtection_AnalogWatchdogISR()`：
读三个 AWD 标志 → 对相应中断实施一次性禁用并清标志 →
`ConvController_TripISR()` 撤销 PWM_EN / TIM1.MOE →
`s_fast_faults` 保存原因。其他 ADC IRQ 交给 HAL 继续处理；
定时 ADC/DMA 采样不会在正常保护报警时停止。

既有 DMA HT/TC RAM ISR 仍实时检查 VBUS/IBUS 饱和，
并完成 CT_RMS/IBUS 零点校准。经过软件校准且硬件模拟
窗口初始化后，前台 `thread_heater` 汇总故障，
`Heater_LatchFault()` 锁存停机，安排音效及短时 OLED
故障覆盖层。NTC_POT、NTC_SYS、VBUS 欠压和 ADC 陈旧状态也在
监督服务中处理。**ISR 不运行 MiaoUI、printf 或播放序列。**

## 5. 验证和后续

- `Statistics → System`：查看 `ZERO`、`CT0`、`IB0`、
  `DAC*`、`AWD2`、`PROT`；`DAC*` 仍是原诊断标签，
  需结合 `comp_armed` 判断硬件是否真正启动。
- Host 测试登记在 `Firmware/Tests/host/run.sh`。
  目前没有完成本轮 Keil 全量编译和实机校准，不能宣称已验证。
- 第一次实验不接加热功率级：查看 RTC/PD/UI 正常，零点采样完成，
  读取 VBIAS 和两个 DAC 实际电压；必要时使用隔离/限流测试源
  验证 PA3 输入进入/离开窗口时 COMP1_OUT（PA6）及 TIM1 BRK 状态。
- 用受控模拟电压分别越过 VBUS 33V 的等效分压、IBUS 门限及
  NTC_SYS 90°C 的等效 ADC 电压，确认中断产生后 MOE 立即关闭，
  ADC 继续更新、故障锁存且不能自动重启。
- 最后才将电流标定、功率级驱动和 TIM1 真实输出纳入验收。
