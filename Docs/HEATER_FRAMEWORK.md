# IH01A HEATER 手动移相测试界面（2026-11-02）

- Main 默认模式：`HEATER_MODE_POWER`，目前表示手动移相角，不是测得电功率(W)。初始 0°，范围 0～180°，L/R 每次变化 10°；角度保存在 Heater 数据中，退出 Main 后仍保留。温度模式 PID 的建议功率暂不映射到移相角。
- 默认选中 TARGET，进入 Main 后 L/R 直接改变角度；ENTER 选择 MODE→TARGET→OFF→ON→RUN；EXIT 退出 Main 不影响 TIM1 PWM。后台计时、预约、RUN 操作均不会使能 PC6。
- 主界面显示输入源、电压、锅底/系统温度、目标角度、CCR3/CCR4、计时、PWM 运行与 PC6 状态。
- TIM1 128MHz，`ARR=426`，上计数约299.77kHz。CH1 PWM1：`CCR1=ARR/2=213`。CH3 Combined PWM2 + CH4 PWM1：`D=round(angle*ARR/360)` 且钳位 ARR/2；`CCR3=D`，`CCR4=D+ARR/2`。两个输出均为 213 / 427 ≈49.88% 占空比。
- `ConvPhase_Calculate()` 为纯整数数学函数。CCMR1/CCMR2 开启 CCR1/3/4 preload，以 TIM1 CR1 UDIS 防止双 CCR 在同一次计数更新中应用不同角度；预加载由下一次自然更新事件生效。
- 在 ADC 零点捕获完成、DAC/COMP 初始化完成、无锁存保护故障且 TIM1 Break BIF 未置位后，后台自动启动 CH1/CH3 逻辑 PWM。CH4 是组合 PWM 的内部参照，不要求物理 CH4 引脚。COMP 的瞬时低电平由硬件 Break 处理，软件不进行持续窗口电平检测。
- PC6=LOW 始终禁止 FD2204 功率级：逻辑 PWM 运行不代表加热。保护故障会先拉低 PC6，再清 TIM1 MOE。当前 `IH_POWER_STAGE_COMMISSIONED=0`、`Heater_GetIsrTargetMw()==0`，Main 无法解除功率级门控。
- 若硬件 Break 已触发或保护锁存，PWM 不自动复位；应检查 TIM1 SR.BIF。测试时先在 PC6 LOW 状态用示波器观察 PA8（TIM1_CH1）与 PA10（TIM1_CH3）。TIM1_CH2 DMA 仍暂不使用。

- 主界面底行 `P:Y/N` 表示 TIM1 逻辑 PWM 输出实际状态，`E:Y/N` 表示 FD2204 的 PC6 使能状态；`E:N` 才是本阶段正确的安全观测状态。

- 初次逻辑 PWM 启动时，COMP/DAC 建立可能留下 BIF 标志。只有零点完成、COMP 已使能、软件保护无故障且 PC6 为低时，才**一次性**清除启动前的 BIF 并尝试使能 TIM1 MOE。若活动 COMP Break 仍存在，硬件会再次置位 BIF 并抑制输出；本轮不自动重试，避免反复重新使能故障后的 PWM。
