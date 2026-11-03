# IH01A · Settings / Heater 手动调试界面

**版本**：2026-11-03（STM32G071、TIM1 128MHz、ADC VREFBUF 2.5V）

## 菜单

根菜单 → Settings → **Heater**，使用 MiaoUI 原生文本数据项，
支持上下滚动；RETURN 返回 Settings。

| 项 | 用法 | 实际动作 |
|---|---|---|
| PC6 Enable | ENTR 切换开关 | 置 ON → Heater_StartConfirmed() 请求启动；置 OFF → 立即拉低 PC6、Heater_Stop() |
| Phase deg | ENTR 进入编辑，L/R 每次减/加 1°，再次 ENTR 确认退出 | 0～180°，立即写 TIM1 CCR1/3/4 预装载，继续使用原有硬件 Break |
| Freq kHz | ENTR 进入编辑，L/R 每次减/加 10kHz，再次 ENTR 退出 | 180～300kHz（默认200kHz）。一旦改频，先 PC6 LOW、取消 RUN，随后停止定时器计数、改 ARR/CCR、UG 装载、恢复逻辑 PWM。**不会自动重新加热** |
| IC2 | 只读 | PA9/TIM1_CH2 上升沿 CCR2 原始计数 |
| Lag | 只读 | 按 ARR/4 + D/2 和模周期计算带符号角，使用当前 ARR，不使用 DMA |
| CT_RMS | 只读 | 去零点电流（A，毫安显示精度），ADC 接近满量程显示 SAT |
| CT0 / ADC | 只读 | 开机零点原始计数和当前低通后计数，便于复核 |
| State | 只读 | STOP / STARTING / FAULT 或上次 RUN 拒绝代码；运行时同时显示 PC6 ODR/IDR 引脚实际状态 |

数据项的普通开关使用 MiaoUI `UI_DATA_SWITCH`；调节使用
`UI_DATA_INT`。数值边界不允许无意回绕（例如180°→0°、
300kHz→180kHz）。所有配置由后台业务共享，退出 Settings 后保持，
Main 也读取相同的移相角；Main 仍按每次10°调节。

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
5. **带电改频**必须在 PC6 关闭后进行：改频操作自动撤销 RUN，
   重新加热需要再次显式操作开关。

本轮不修改 PD 默认12V/3A、安全保护门限以及 TP1941 链路极性。
已登记 Host 纯函数单元测试和 Keil 工程文件；完整 Keil
Rebuild、RMS 模拟前端增益及板级中断时序仍需实测。
