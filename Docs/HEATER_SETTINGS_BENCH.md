# IH01A · Settings / Heater 手动调试界面

**版本**：2026-11-03（STM32G071、TIM1 128MHz、ADC VREFBUF 2.5V）

## 在线同步更新原则

当前TIM1初始化为PSC=0、RCR=0、`ARPE=1`，PWM启动前确保
`OC1PE=1`、`OC3PE=1`、`OC4PE=1`。改频采用如下事务：

```
UDIS=1
ARR <- new_arr
CCR1 <- new_ccr1
CCR3 <- new_ccr3
CCR4 <- new_ccr4
UDIS=0
// TIM1 CEN=1: 下一次自然 UEV 同步装载所有预装载值
// TIM1 CEN=0: 仅停机时允许 EGR.UG 一次性初始装载
```

不在运行中写`EGR.UG`、`CNT`或清`CEN`，也不修改`PC6`。
原`COMP→TIM1 Break`通路依然有效。若运行中发现预装载
未使能，则拒绝调频并立即关断PC6、撤销RUN。不要把这个同步更新
等同于谐振腔的物理换流无尖峰保证。

## 菜单

根菜单 → Settings → **Heater**，使用 MiaoUI 原生文本数据项，
支持上下滚动；RETURN 返回 Settings。

| 项 | 用法 | 实际动作 |
|---|---|---|
| PC6 Enable | ENTR 切换开关 | 置 ON → Heater_StartConfirmed() 请求启动；置 OFF → 立即拉低 PC6、Heater_Stop() |
| Phase deg | ENTR 进入编辑，L/R 每次减/加 **10°**，再次 ENTR 确认退出 | 0～180°，CCR1/3/4 预装载，下一次 TIM1 更新事件同步生效 |
| Freq kHz | ENTR 进入编辑，L/R 每次减/加 10kHz，再次 ENTR 退出 | 150～400kHz（默认200kHz）。**运行中不关闭 PC6、不停 TIM1**，ARR/CCR1/3/4 预装载在下一次自然更新事件同步生效；保护继续工作 |
| IC2 | 只读 | PA9/TIM1_CH2 上升沿 CCR2 原始计数 |
| LagC / LagR | 只读 | LagC 为最新三段线性补偿角（模型适用时）；LagR 为原始 Lag，均由 TIM1_CH2 捕获换算，不使用 DMA。空载拟合适用范围之外 LagC 显示无效原因 |
| CT_RMS | 只读 | 去零点电流（A，毫安显示精度），ADC 接近满量程显示 SAT |
| CT0 / ADC | 只读 | 开机零点原始计数和当前低通后计数，便于复核 |
| State | 只读 | STOP / STARTING / FAULT 或上次 RUN 拒绝代码；运行时同时显示 PC6 ODR/IDR 引脚实际状态 |

数据项的普通开关使用 MiaoUI `UI_DATA_SWITCH`；调节使用
`UI_DATA_INT`。数值边界不允许无意回绕（例如180°→0°、
300kHz→180kHz）。所有配置由后台业务共享，退出 Settings 后保持，
Main 也读取相同的移相角；Main 和 Settings 均按每次10°调节。
串口 CLI 可按 1° 调整相移、按整数 1kHz 设置 150～400kHz；
详见 [HEATER_CLI_PROTOCOL.md](HEATER_CLI_PROTOCOL.md)。

**注意：相位检测补偿不修改 TIM1、功率级使能或硬件过流判据。**
当前 LagC 来自 [ZCD_PHASE_COMPENSATION.md](ZCD_PHASE_COMPENSATION.md) 的
IC2 计数域三段线性模型，仅在160～330kHz、120～180°和CT_RMS≥0.050A
（ADC有效）时显示；移相/调频仍支持原有全范围。显示 LagC 并不代表
带锅负载的相位检测误差或 ZVS 裕量已经经过标定。

**PC6 开关不是绕过软件保护的 GPIO 开关**。置 ON 只提交显式
RUN 请求，由 `ConvController_BenchRunPoll` 核对有效的12V、
≤3A PD合同，VBUS 10.8～13.2V、零点校准、温度、DAC/COMP
和 TIM1 Break 后才能真正拉高 PC6。没有获准或保护触发会
自动回到 OFF/FAULT。原用户取消的启动计时和相位限制**没有恢复**。

## CT_RMS 计算

已知 `CT_RMS` 模拟量每安培变化 `0.25253V`；基准来自每次
开机未开关功率级时对该通道的 2048 次采样平均。
显示使用后台 ADC 低通值 `adc.filtered[CONV_ADC_CT_RMS]`，
零点为 `ConvProtection_GetSnapshot().ct_zero_raw`。

```
delta = max(0, ADC_filtered - CT0)
I_mA = round(delta * 250000000 / (4095 * 25253))
```

该公式由 `VREF=2.5V`、12-bit 量化及 `0.25253V/A` 直接得到。
**1 个 ADC 计数约为 2.418mA**；本次只用于电流显示，
不参与硬件 COMP 过流阈值（仍独立维持±12A测试配置）。
它假定 CT_RMS 模拟前端输出已与有效值电流线性对应；未通过
正弦电流注入标定前，不应把它视为计量级 True RMS 测量。

## 防休眠

当前 PM 框架的 `pm_api_set_sleep_block()` 已支持按位阻止
自动关屏。PC6 真正使能后设置 `PM_BLOCK_HEATER`，
因此 `PM_STATE_RUN` 保持活动、自动休眠倒计时暂停；
根菜单 BACK 的手动关屏也因该位而拒绝执行。最末 idle 任务
在 PC6 开启时跳过 `__WFI()`。手动 OFF/保护关断后，
前台释放 PM_BLOCK_HEATER 并恢复原休眠设置。

关断始终由 PC6 LOW 优先，且 TIM1 Break 与 ADC 快速
故障仍各自有效。中断路径不调用 UI 或 PM 控制器。
保护中断的 PC6 LOW 与 PM 解除之间最多相差一个前台轮询周期，
这只可能让屏幕多保持一小段时间，不会导致输出重新使能。

## 验收

1. 保持 PC6 关闭，设置200→220→200kHz，确认
   `ARR=639→581→639`，CH1 占空比约50%，CCR3/4与当前角一致。
2. 检查 Settings/Heater 的 Phase 每1°调节，Main 每10°调节，
   两者共享目标；检查频率、角度不会在上下端意外回绕。
3. 确认 ZERO 为 READY；CT0 和 ADC 在 PC6 关闭时接近，
   CT_RMS 显示接近0A；然后使用已知电流与独立测量仪对比。
4. 确认用户在 Settings 切换 PC6 ON 后，仍仅在通过原有保护
   许可时使能；OFF 立即关断。加热中空闲超过此前休眠时长
   仍保持屏幕可用；停止后恢复正常睡眠。
5. 初期保持PC6关闭，用示波器检查180→200→220→200kHz的每次换频。
   确认PA8/PA10波形没有混合CCR或计数器被强制重置的短周期。
   再在已经验证的感性低功率工作区尝试小幅带电改频，并同时测量
   两桥臂VDS、谐振电流、母线电压。出现非ZVS、浪涌或故障立即停止。
   正常改频**不撤销RUN、PC6保持高**；检测到preload配置异常则拒绝
   改频并关断PC6。
6. 需要注意：寄存器同步生效只保证PWM时序一致，不能证明改频瞬间
   仍符合ZVS要求。保护与电流相关风险必须由示波器/硬件验证。

本轮不修改 PD 默认12V/3A、安全保护门限以及 TP1941 链路极性。
已登记 Host 纯函数单元测试和 Keil 工程文件；完整 Keil
Rebuild、RMS 模拟前端增益及板级中断时序仍需实测。

## 2026-11-10：Pan Detect 缩短脉冲

- 实测 12V 供电下，旧 150kHz/180°/6周期（约40µs）激励期间发生 `0x00004000`，对应 `CONV_PROT_TIM1_BREAK`（硬件比较器经 TIM1 Break 关断桥臂）。示波器在槽路监测点测到约158kHz的高幅值振荡；电压测量处于超量程/截顶状态，未确认峰值准确性与触发瞬间的电流，因此不将此认定为已证明的 ±12A 实际峰流。
- 按测试要求，检锅专用频率从 **150kHz 调整到160kHz**（128MHz TIM1时钟，`ARR=799`），同时保守地把使能长度从 **6周期缩短至2周期**（标称12.5µs）。相移仍180°，间隔仍2s；TIM1持续运行，PC6脉冲后继续IC2 DMA捕获。数据日志 `[PAN]` 的频率/周期数随常量自动更新。
- 该频率与观测的158kHz振荡频率十分接近，**不能保证离开谐振区，也不能保证避免TIM1 Break**。真实PC6脉冲宽度仍受TIM1 IRQ延迟影响。硬件COMP/TIM1 Break、ADC保护、故障锁存、手动重启条件均不放宽。
- **实验前确认地线/差分测量方法**：普通接地台式示波器的地夹不得跨接两个悬浮功率节点。推荐使用适当额定值的差分探头及独立的CT/电流探头，并先在低能量条件下验证PC6上升/下降、实际振荡和保护行为。出现重复Break或未知高压尖峰应停止加能测试。
