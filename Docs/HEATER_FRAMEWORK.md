# IH01A Heater 主界面 / 后台控制架构

## 已实现的边界

- `Main` 进入独立的 `Heater_UI_Page()`，屏幕标题只用 `HEATER`；`IH01A` 保留在 About。
- ENTER 在 `MODE → TARGET → OFF → ON → RUN` 五个域中切换，L/R 改变当前域；EXIT 退出 Main，不停止已配置的预约与定时关闭。
- MODE=TEMP/POWER；TARGET=锅底摄氏温度(40–120°C)或用户功率(进入 POWER 模式时自动跟踪当前输入预算，首次 L/R 修改后锁定用户上限（10W 步进）)；OFF=自动停止时长(0–240分钟)，ON=相对预约等待(0–1440分钟)，RUN 域 R 请求启动、L 立即停止。
- 预约到期进入 `HEATER_WAIT_CONFIRM`，**不会自动启动功率输出**。需回到 Main 选 RUN、按 R 人工确认。因为锅具检测和功率控制 ISR 尚未经过安全验证，继续保持 `Heater_GetIsrTargetMw()==0`。页面上的 P x/yW 是建议功率和预算，并非实际测量功率。
- `thread_heater` 100ms 执行一次，独立于 OLED 和 MiaoUI 页面；温度闭环采用定点 PID（100ms；D 对锅底测量值求差分并做 Q4 低通滤波，避免目标突变带来的微分冲击），积分有抗饱和限制；三项系数目前仅供监视态实验，未进行热负载整定。转换过程仅调用 `TempService_GetSnapshot()`，不在 ADC 20kHz ISR 中做温度查表或浮点。
- `Heater_BudgetClassify`：验证有效 PD合同后，按其电压*电流确定上限；无有效PD且 PD_InputSnapshot.mode=DCIN、VBUS≥8V 时按 VBUS*7A；协商中/欠压时预算为0。附加临时软件上限270W，未来需根据 IH01A 实际功率器件定标。用户在功率模式设定较低功率，最终建议功率取 min(用户目标, 当前预算)。不能把合同额定功率当作真实消耗。
- `thread_idle` 必须为调度器最后一项：50ms 调用 `pm_api_poll()`，检查中断状态后执行 `__WFI()`；SysTick、ADC、PD 中断等能唤醒，再 `THRD_YIELD`。USB-PD 非RTOS主循环仍在 WFI 返回后执行。
- 当前 `MX_RTC_Init()` 会在每次复位时清空时钟：预约按 `HAL_GetTick()` 的相对分钟执行，**不支持跨MCU复位**。未来实现绝对预约应先修复 RTC 时间保持与设时。

## 安全与待验收

- `PWM_EN` 及 TIM1.MOE 仍由原 `ConvController_Init()` 禁用。建议功率绝不直连 TIM1；硬件联锁、缺锅判断、功率回授、温度保护等测试完成后才能审查启用。
- 当前温度有效性判定：POT+SYS 双通道均有效，最近更新≤500ms，SYS<85°C、POT<130°C（仅软件试验阈值，**非已验证的安全工作温度**）。异常时锁存 `HEATER_FAULT`，不发送功率控制目标。需物理核实热滞后、温控 PID 系数与临界值。
- Keil Rebuild 以及实机中断/计时/PWM关闭状态未在本环境执行。测试脚本新增 heater 单元测试。
