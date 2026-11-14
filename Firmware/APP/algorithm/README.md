# IH01A APP/algorithm — PD 策略层

与 ST USB-PD FULL 中间件分离的应用层策略。当前支持 SPR Fixed PDO 和 **实验性 28 V/5 A Fixed EPR**；历史 MINSNK “EPR 未实现”叙述已过期。

| 文件 | 职责 |
|---|---|
| `pd_input.c/.h` | CC 会话、SPR/EPR 请求、输入源状态、ADC VBUS 与恢复 |
| `pd_select.c/.h` | PDO 选择和 RDO 构建的纯函数 |
| `pd_source_stats.c/.h` | Source PDO 快照与只读格式化 |
| `pd_diag.c/.h` | 协议诊断与日志 |
| `pd_epr_policy.h` | EPR 功能策略及硬件许可相关配置 |
| `ntc_lut.c/.h` | 温度换算表（与 PD 本身无关） |

ST DPM 回调必须非阻塞，不在时序敏感路径执行打印或屏幕刷新；USB-PD 时基由 SysTick 驱动。MiaoUI 层只使用快照与受控操作接口，不直接操控协议栈内部状态。

**实验边界：** 28 V/5 A 是协议层合同能力，不能代表功率级已实现稳定 140 W 加热；EPR 保持、CC、线缆及 VBUS 路径的边界参见 [PD 验证](../../../Docs/PD/README.md)。
