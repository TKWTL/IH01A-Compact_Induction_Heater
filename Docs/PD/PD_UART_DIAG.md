# C140 5V -> 20V reset diagnosis: UART log bring-up

1. Connect PB6 (USART1 TX) to a 3.3V USB-UART adapter RX, PB7 to
   adapter TX, plus common GND. Do **not** connect adapter +5V/VCC to
   the board. Terminal: **115200 8N1**.
2. Pull `main`, perform a full Keil Rebuild and program the firmware.
   The first line prints captured RCC->CSR reset flags. RCC reset
   flags are cleared once at boot, so the *next* boot can distinguish
   POR/BOR from pin/software/watchdog reset.
3. Input `ping` followed by Enter. Expect `[CLI] pong`; this tests
   UART RX circular DMA, IDLE and TX DMA. Use `uart` to check
   dropped bytes and errors.
4. Connect the C140 with heater PWM disabled. Capture the
   `SPR_CAPS`, `REQUEST`, `NOTIFY`, `CONTRACT` and
   `HARD_RESET` lines, especially **before** any 1 second VBUS gap.
   `REQUEST.a` is mV, `REQUEST.b` is the RDO in hexadecimal;
   e.g. expected normal 20V/3A SPR Fixed request
   `REQUEST a=20000 b=0x5104B12C` when PDO5 is 20V.
   The 5V source PDO bit 23 advertises EPR support, which is **not**
   a sign that the currently linked MINSNK firmware has entered EPR.
5. If the board resets, compare first/second boot reset flags and
   observe external VBUS, 3.3V and NRST on an oscilloscope. A source
   Hard Reset vs local brownout is **not** distinguishable from UART
   reset messages alone.
6. Collect `pd` and `uart` outputs once 20V becomes stable.
   If UART drops/events are frequent, reduce other debug output;
   there is deliberately no blocking backpressure.

The board currently has nonblocking 512B TX/RX software rings and a
128B circular receive DMA buffer. DMA1 Channel4/5, PB6/PB7 and
USART1 IRQ are configured consistently in CubeMX .ioc and Core code.

This change is diagnostic only; it does not alter the 3A request
policy or enable EPR protocol negotiation.

## 新增诊断流程（2026-11-01）

- 上电时即观察示波器 PB6：应先发送一次短 `U1OK` 标记，再由 DMA 发送 `[BOOT]` 与 `[UART]`。不要只用直流表测 UART：它绝大部分时间处于空闲高电平。
- Keil Watch 检查 `g_uart_boot_stage` 和 `g_uart_probe_result`；前者若停在 1 或 2，先定位启动卡点。若到 5 而无后续日志，检查 TX DMA/中断处理。
- 串口命令 `uart` 能输出 DMA 启动、完成、失败计数。
- 仅允许 **boot probe** 使用一次短 HAL 轮询发送，所有正常运行期间及 PD 回调都保持无阻塞日志；必要时注释 probe 再检查 PD 首次应答时序。

## 主机侧收发与解析（COM19 / WCH-Link，2026 实测）

主机侧工具：`Tools/serial_cli.py`（pyserial，Windows 独占打开 COM19）。

```text
python Tools/serial_cli.py --port COM19 --cmd help --cmd ping --cmd pd --cmd uart
python Tools/serial_cli.py --port COM19 --cmd pd --json
python Tools/serial_cli.py --port COM19 --listen 5 --hex
python Tools/serial_cli.py --port COM19 --repeat 10 --cmd pd --retries 8   # 链路压力测试
```

只读命令为 `help`/`ping`/`pd`/`uart`；`pd on|off|max N|epr|auto` 会真实改变
USB-PD 协商状态，必须显式加 `--allow-write` 才会发送。

实测要点（均为本机 COM19：`WCH-Link SERIAL`，USB VID:PID=1A86:8012）：

1. MCU 侧正常：干净帧里 `uart` 报告 `rx_drop=0 rx_err=0`，
   `tx_start` 与 `tx_done` 恒等。
2. **乱码的根因在主机读法，不在固件、也不在链路。**
   用 `Serial.read()` 的阻塞读法时，WCH-Link 的接收缓冲会在主机读取
   的间隙里溢出，解码出来的字节从大约第 45–95 字节开始整体位错位
   （`[CLI]` 变成 `[\xab\xb1\xc1\xb1`），乱码里甚至夹着 `U1OK`、
   `Fixed 12.0 20.0V 5.00A` 这类旧内容，很容易误判为固件 TX 环缺陷
   或 EMI。**改用 `in_waiting` 忙轮询、有数据立刻读走**后，同一批
   流量逐字节完全复现：

   | 读法 | `heater get`（94 B）|
   |---|---|
   | 阻塞 `read()`，timeout=20 ms | 8/8 帧损坏（首坏字节 56–70）|
   | `in_waiting` 忙轮询 | **8/8 帧干净，长度恒为 94 B** |

   实测前一轮把原因写成"桥接/EMI"是错判，已在此更正。改读法后
   450 点扫描全程 **0 坏帧、0 次重发**。
3. 由此产生的两条铁律：
   - **不要**用阻塞 `read()` 收这条串口的数据，要用
     `while: n=in_waiting; if n: read(n)`；
   - **不要**在传输中途调用 `reset_input_buffer()`（会造成失步）。
4. 仍保留**按帧校验 + 重发**（`--retries`）作为兜底：只接受满足完整
   报文语法的帧（HCLI 用正则整体匹配），从而连"静默丢字节但仍是
   ASCII"的帧也能拒掉——只查"是否全为可打印字符"是不够的。
5. `uart` 的 `idle`（RX 空闲中断）计数会随交互快速增长，属正常现象
   （每次命令后线路回到空闲都会产生一次 IDLE），不是毛刺证据。

## 2026-11-06：28V 大负载时 MCU 重启取证

每次真正进入 MCU 启动流程，USART1 先直接发 `U1OK`，随后从非阻塞 DMA 队列发一行，例如：

```text
[RESET] CSR=08000000 PWR=1 PIN=0 SW=0 IWDG=0 WWDG=0 LPWR=0 OBL=0 SRC=NONE STAGE=0
```

示例仅用于展示格式，真实值由 RCC->CSR 决定。

- STM32G071 `PWRRSTF` 同时涵盖 POR/PDR/BOR；`PWR=1` 无法进一步区分上电还是欠压复位。须用示波器同步测量 VBUS、3.3V 与 NRST。
- `SW=1 SRC=ERROR_HANDLER/ASSERT/HARDFAULT/NMI/EMERGENCY` 表示本固件某个故障入口通过 `NVIC_SystemReset()` 触发软件复位。STAGE 是故障发生时的 `g_uart_boot_stage`，不是程序计数器。
- `SW=1 SRC=OTHER_SW` 表示软件复位位有效，但没有可信的本地故障记录；可能由别处发起，不能归因为上述入口。
- `PWR`、`PIN`、`SW`、`IWDG`、`WWDG`、`LPWR`、`OBL` 可能多位同时为 1；原始 CSR 仍然打印，不能只按一个字断定。
- 4 个 32 位字段在 SRAM2 的 UNINIT 保留区 `0x20008E80..0x20008E9F` 中，利用魔数、原因和反码验证，只在 `SW=1` 且 `PWR=0` 时读取，启动即消费并清除。真正掉电可能使保留数据丢失，不能把 SRC=NONE 解释为排除软件问题。
- 故障/ISR 路径先关闭 PC6，写 4 字 SRAM 记录并直接请求系统复位；**不在中断里 printf 或阻塞等待 UART**。正常上电仅有既有的 `U1OK` 轮询发送，后面的 RESET 行由 DMA 异步输出。
- 需要持续读取原始串口字节：电磁干扰或上位机缓存溢出也可能造成伪 `U1OK`。记录实际复位时间、前后两次 RESET 记录和示波器波形，并和 USB-PD Hard Reset 对照。
