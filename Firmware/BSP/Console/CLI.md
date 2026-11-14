# IH01A UART CLI（调试接口）

USART1 PB6 TX / PB7 RX，**115200 8N1**；以 ASCII 小写命令加换行符发送。此接口用于**受控实验**，不是解除保护的命令通道。功率故障、EPR 协商异常、ADC/COMP 失效时，加热请求应失败而不是绕过联锁。

| 命令 | 用途 |
|---|---|
| `help`、`ping` | 列出命令、测试串口 |
| `heater get` | 获取 HEATER、PHASE、FREQ、IC2/LAG、CT_RMS、IBUS、PAN 等快照 |
| `heater phase 180` | 移相角 0～180° |
| `heater freq 200` | 手动设定 150～400 kHz；自动调频使能时禁止 |
| `heater auto off` / `heater auto on` | 关闭/请求启用自动频率；当前 CT/ZCD 未重新校准时 `auto on` 返回 `CT_ZCD_RECAL_REQUIRED` |
| `heater on` / `heater off` | 请求有条件使能 / 立即关闭 PC6（`on` 不绕过保护） |
| `pd`、`pd on`、`pd off` | 查看 PD 状态、手动启停 PD |
| `pd max 20` | 设置 PD Fixed 电压申请上限，单位 V，范围 5～31 V |
| `pd auto`、`pd epr` | 切换下次接入时的 EPR 自动进入 / 当前人工发起 EPR |
| `uart` | 串口统计 |

**禁止将 `pd epr1` 当作正常使用命令**：它是历史诊断对照路径，曾导致源端 Hard Reset。常规 EPR 使用 `pd epr`。

示例：

```text
heater get
heater auto off
heater phase 90
heater freq 200
heater off
```

Heater 命令回复以 `HCLI OK` 或 `HCLI ERR` 开头，例如 `HEATER_BLOCK=NOT_READY`、`RANGE`、`SYNTAX`。PD 调试日志可能与响应交错，上位机应按前缀过滤；`heater get` 只查询，不改变状态。上位机示例：[Tools/heater_cli_client.py](../../../Tools/heater_cli_client.py)。

**重要诊断语义**：`LAG_RAW_DEG` 是旧 CT/ZCD 链路的观测角，`LAG_DEG` 与 `LAG_QUAL` 受标定资格约束；有效 CCR2 捕获不保证 ZVS。 `IBUS` 显示值是采样/滤波电流，ADC AWD2 与 TIM1 BRK 负责独立快速保护。有关短脉冲与测量边界参见 [实验报告](../../../Docs/README.md)。

源码：`heater_cli.c`、`heater_cli_parse.c`、`console_cli.c`。保留原文命令与字段名称，以兼容自动化脚本。
