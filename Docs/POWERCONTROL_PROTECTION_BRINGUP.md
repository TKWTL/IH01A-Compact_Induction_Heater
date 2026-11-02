# IH01A PowerControl 保护与 ADC 零点校准阶段

## 从旧版代码迁移的边界

原 `PowerControl/Legacy/conv_protection.legacy.c` 属于 STM32G474/HRTIM：
`Protection_Init1` 通过停止 ADC 修改 AWD；`Protection_Init2` 实际为空；
`HRTIM_FLT_IRQHandler` 依赖 G474 HRTIM FLT1。不能直接调用到 STM32G071。
此次使用 G071 现有 TIM6/ADC1/DMA 双帧和独立的 TIM1 输出门控。

已知 CubeMX 现状：TIM1 只配置 **COMP1 → TIM1 BRK**，COMP2 尚未配置到
TIM1 BRK/BRK2；DAC1 CH1/CH2 分别选为两个 COMP 的负输入。
比较器正输入硬件连接、比较器输出极性、外部前端传递函数与故障
实际关断链路还需实机核实，不能宣称双 COMP 过流保护已生效。

## 启动顺序

1. `main()` 只执行 CubeMX 外设初始化、PD 中间件初始化和 `APP_Tasks_Init`。
   GPIO 初始化已经拉低 `PWM_EN`；TIM1 MOE 也保持禁用。
2. 第一次 `TempService_Poll()`（已进入 USBPD foreground 调度链）：
   `ConvProtection_Init()` → `ConvController_Init()` →
   `ConvADC_Start()`：ADC 校准 → DMA → TIM6 触发。只执行一次，失败进入
   `CONV_STATE_FAULT`。
3. DMA ISR 在未开启功率级时忽略最初 256 帧，再对 CT_RMS/IBUS 的
   2048 帧原始 ADC 值分别计算算术平均，得到 `ct_zero_raw` 和
   `ibus_zero_raw`。每路区间峰峰值必须不大于 160 LSB，
   CT_RMS 不允许饱和。**必须确认 PWM_EN=0 且 TIM1 MOE=0**，
   一旦检测到开关动作，拒绝零点校准并锁存故障。
4. `thread_heater` 每 100ms 调用 `ConvProtection_Service()`，
   从 DMA ISR 标志汇总故障，并检查 VBUS、NTC_POT、NTC_SYS。
   异常时 `Heater_LatchFault()` 清零建议功率；前台蜂鸣器入队，
   并在 OLED 展示约 4 秒的故障信息覆盖层。

**零点数据目前只做校准与诊断，不会自动引入未经校准的安培转换。**
后续电流测量应使用 `raw - zero_raw` 和实际测出的电流增益。

## 保护层级

| 层级 | 当前实现 | 尚未完成 |
|---|---|---|
| 瞬时硬件过流 | TIM1 Break 配置中含 COMP1 路由，但 COMP/DAC 未启动 | COMP1/2 输入极性、DAC阈值及真实过流测试 |
| 快速 ADC | DMA ISR 检测 VBUS/IBUS 饱和，立即撤销 TIM1 MOE 与 PWM_EN | G071 AWD1/AWD2/3 中断独立关闭路径；真正安培数值阈值 |
| 后台保护 | 100ms VBUS 欠压、NTC 无效或过温、ADC 陈旧 | 温度范围的最终标定、热惯性实验、功率/电流增益标定 |
| 故障 UI | 只由前台显示覆盖层与两声提示音，故障状态锁存 | 完整 MiaoUI 弹窗事件队列和人工确认恢复流程 |

注意：VBUS 为 ×16 分压、ADC 2.5V 参考，在母线约 40V 时 ADC
已经到达满量程。当前 `CONV_PROT_VBUS_RAIL_RAW=4070` 是**接近饱和**
检测（约 39.8V），不能检测超过满量程后具体是多少伏。
IBUS 的 4090 原码阈值只表示 ADC 接近上轨，并非经过安培标定的过流阈值。

## ARR、VBIAS、DAC1 提案

提供 `ConvProtection_CalcDacWindow(vbias_raw, arr, low_delta,
high_delta,...)` 纯整数转换，其中 `low_delta/high_delta` 是
**在 TIM1 ARR=853 时通过外部实验得到的 DAC 码值跨度**。
新 ARR 下按 `(ARR+1)/854` 缩放，输出只保存在状态快照中。
未标定前 `dac_profile_ready=0`，两个 COMP/DAC 通道**不启用**。
这个关系是预留的*参数化假设*，不是从现有 PCB 自动推导出的真实
电流阈值公式，必须结合采样前端与 COMP 输入极性完成标定后修改。

推荐测试：不接功率级电源，确认 VBUS/IBUS/CT_RMS 在调度后采样；
检查统计页 `ZERO` 从 WAIT → AVG → OK，查看 CT0/IB0；
然后用示波器确认 TIM1 PWM_EN/MOE 在整个窗口都没有动作。
检验温度开短路、模拟 VBUS 异常和带符号原始电流误差，
最后才评估 DAC+COMP 与真正的高频电流保护。

目前 `Heater_GetIsrTargetMw()` 始终返回 0、不会使能加热。
