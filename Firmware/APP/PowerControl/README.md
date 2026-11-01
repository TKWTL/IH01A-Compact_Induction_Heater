> ⚠️ **内部资料**：本仓库为学校内部学习与竞赛资料，仅供在校师生使用。
> 未经授权，禁止外传、复制、转载或用于任何商业用途。

# PowerControl — 数字电源控制模块

## 信号链

```
ADC ──→ Controller ──→ Loop ──→ PWM
  ↑         │            │        │
  └─────────┴──── ← ─────┘        │
                                   ↓
                               Hardware
```

基本环节：

1. **ADC** — 采样电压/电流
2. **Controller** — 状态管理、启停调度
3. **Loop** — 闭环控制算法（PR/PID）
4. **PWM** — 驱动输出到硬件

---

## 文件职责

| 文件 | 角色 | 说明 |
|------|------|------|
| `conv_adc.c/h` | **ADC** | 电源专用 ADC 采样与滤波，提供电压/电流读数 |
| `conv_controller.c/h` | **Controller** | 电源状态机（启/停/故障），调度 ADC→控制→PWM 的时序 |
| `conv_loop.c/h` | **Loop** | 闭环控制算法（PR 控制器），计算占空比/调制量 |
| `conv_pwm.c/h` | **PWM** | PWM 驱动输出，更新占空比至定时器 |
| `conv_protection.c/h` | **Protection** | 监控过压/过流/过热等故障，保护时切断 PWM 或通知 Controller 进入 FAULT 状态；同时提供参数配置接口（自动恢复使能、延迟、重试次数） |
| `dashboard.c/h` | **Dashboard** | 表层 UI 接口，向 `Applications/` 层暴露电源状态的只读/可调参数，不直接操作硬件 |
