# 交接：IH01A USB-PD SPR + EPR（28 V/5 A）现状与下一棒任务

> 本文是**当前状态**的自包含交接；历史演进见 `Docs/HANDOFF_PD_EPR_FULL.md` 与
> `Docs/USBPD_FULL_EPR_IMPLEMENTATION_STATUS.md`。写于 2026-11-02。

## 0. 30 秒速览

- 分支 `feature/pd-epr-28v5a`，基点 `main@668e541`，`main` 未被改动。
- 当前能力：接入后自动建立 **SPR 20 V/5 A** 合同，250 ms 后自动尝试 **EPR**，
  成功则到 **28 V/5 A（140 W）** 并周期性 KeepAlive；任何一步失败都退回 20 V。
- 板端已实测：连续 **6 小时**无断联；逻辑分析仪（4 MSa/s）确认完整
  `SPR → EPR_Mode → EPR_Caps → EPR_Request → PS_RDY → KeepAlive` 时序合规。
- 编译：Keil MDK/armclang（`D:\Program Files\Keil_v5`），批量 Rebuild 命令见 §4。
  最近一次已实测 Rebuild（`6e9cdf7`）：`0 Error(s), 0 Warning(s)`，`Code=80,712 RO=4,200 RW=476 ZI=22,756`。本次开关调整尚待 Keil Rebuild。
- host 单测 7/7 通过；PulseView 的 PD 解码器已升级到支持 PD3.1/3.2（见 §7）。

## 1. 板端已实测、可以当结论用的事实

| 项 | 数据 |
|---|---|
| PD 库 | vendored `STM32_USBPD_Library_v541`（core `aafa359`，FULL lib 140,892 B），`LIB_ID=0x30410000` 校验通过 |
| 内存 | `reqmem=4506`（2 口口径，`USBPD_PE_GetMemoryConsumption()`），microLIB 堆 8 KiB |
| SPR 合同 | `REQUEST 20 V/5 A`，RDO `0x51C7D1F4`（OPOS=5、no_suspend、unchunked、EPR capable） |
| EPR 合同 | `EPR_Request 28 V/5 A`，RDO `0x81C7D1F4`（**OPOS=8 绝对位置**），CopyPDO `0x0008C1F4` |
| 源端能力（本机适配器） | 9 个对象：5 V/3 A、9 V/3 A、12 V/3 A、15 V/3 A、20 V/5 A、PPS 5–21 V、空、**28 V/5 A**、**EPR AVS 15–28 V/140 W** |
| KeepAlive | **栈自身约 500 ms**；4 MSa/s 抓包未发现 375 ms 应用补发帧，应用探针默认关闭 |
| 长时间稳定性 | 6 h 无断联、无 Hard Reset |

## 2. EPR 能打通的 4 个必要条件（**改动前务必读完，别回退**）

这四条都是踩出来的，每条都有对应的失败现象：

| # | 条件 | 代码位置 | 违反时的现象 |
|---|---|---|---|
| 1 | `USBPD_HW_IF_IsResistor_SinkTxOk()` 返回 TRUE | `Devices/.../usbpd_phy_hw_if.c`，宏 `IH01A_PD_FORCE_SINK_TXOK` | 所有**应用主动发起**的报文被静默丢弃（`EPR_ENTER a=0` 后无任何 `PD_TX`，之后所有 API 返回 BUSY）。本板外部 Rd，UCPD 的 CC vstate 永不报 Rp-3.0 A，所以必须补偿 |
| 2 | `USBPD_CORE_DATATYPE_REQ_COPYPDO` 只回 **4 字节**（被复制的源端 PDO） | `usbpd_dpm_user.c` + `PD_Input_EPR_CopyPDO()` | 回 8 字节会让 EPR_Request 的 DO2 变成 RDO 副本，源端直接 Hard Reset |
| 3 | 请求 RDO 置 `UnchunkedExtendedMessage`(bit23) + `EPR_Capable`(bit22) | `PD_EncodeFixedRDO()` | 缺 bit23 时源端把 EPR_Request 当协议违规，Hard Reset（不发 Reject） |
| 4 | Object Position 用**绝对位置 8..13** | `PD_Input_Evaluate()`（`s_epr_opos_absolute`，默认 1） | 用 1 基下标 1..6 同样 Hard Reset；`pd epr1` 可复现 |

其它必需设置（都在 `usbpd_dpm_conf.h` / `usbpd_dpm_user.c`）：

- `Is_EPR_Supported_SNK=1`、`PE_UnchunkSupport=1`、`Is_SrcCapaExt_Supported=1`；
- `USBPD_CORE_DATATYPE_SNK_PDP_EPR` 返回 1 字节 = `IH01A_EPR_SINK_PDP_W`（140）；
- `USBPD_CORE_DATATYPE_SNK_PDO_EPR` 返回 32 字节（SPR PDO1..5 + 位置 8 = 28 V/5 A）；
- EPR 入口**必须从 PE 回调上下文登记**：先用 `USBPD_PE_Request_CtrlMessage(GET_SRC_CAP)`
  制造一次 `Source_Caps → Request → PS_RDY` 周期，再在 `POWER_EXPLICIT_CONTRACT`
  通知里调用 `USBPD_PE_Request_EPRModeEnter()`。从空闲主循环调用会让库把
  EPR 通用状态机永久挂起（反汇编确认：该 API 只写 `0xA8` 到句柄并唤醒 PE，
  真正发送在 `PE_STATE_GENERIC` 分支）。
- KeepAlive：由 ST FULL 栈管理，板端实测约 500 ms，6 小时无断联。原来在
  `PD_Input_EPR_Poll()` 里尝试的 375 ms AMS 从未出现在 CC 上，已将
  `IH01A_EPR_APP_KEEPALIVE_PROBE` 默认设为 0；仅调试时可启用有次数上限的探针。

## 3. 代码地图

| 文件 | 职责 |
|---|---|
| `Firmware/APP/algorithm/pd_input.c` | SPR/EPR 状态机：能力解析、选档、RDO 编码、EPR 阶段推进、可选 KeepAlive 诊断探针、CLI 运行时开关 |
| `Firmware/APP/algorithm/pd_select.c` | 纯 PDO 选择（host 可测）；`PD_INPUT_SPR_MAX_MA` 等常量在 `pd_select.h` |
| `Firmware/APP/algorithm/pd_epr_policy.h` | **所有策略开关**：`IH01A_EPR_28V5A_HARDWARE_APPROVED`、`AUTO_ENTER`、`IH01A_PD_SPR_MAX_MA`、`IH01A_EPR_SINK_PDP_W`、`IH01A_EPR_REQUEST_MA`、`IH01A_EPR_OPOS_ABSOLUTE_DEFAULT`、`IH01A_PD_FORCE_SINK_TXOK` |
| `Firmware/USBPD/Target/usbpd_dpm_user.c` | `GetDataInfo`/`SetDataInfo`（EPR PDP/PDO/CopyPDO）、通知分发、`RequestWhatToDo`、诊断打点 |
| `Firmware/USBPD/Target/usbpd_dpm_conf.h` | `DPM_Settings`（PE 特性位、PDP）；`DPM_USER_Settings` |
| `Firmware/USBPD/App/usbpd.c` | 启动自检打印（`LIB_ID`、`PE cfg` 三个 EPR 开关） |
| `Firmware/APP/algorithm/pd_diag.{c,h}` | 诊断队列 + `PD_TX`/`PD_TXDO`/`PD_RX`/`TXDONE` 打点、KeepAlive 时间戳 |
| `Firmware/BSP/Console/console_cli.c` | 串口命令 |
| `Devices/.../usbpd_phy_hw_if.c`、`usbpd_phy.c`、`usbpd_hw_if_it.c` | **对 ST 设备层的唯一改动**：SinkTxOK 补偿 + 收发探针（体积很小，注释齐全） |
| `Firmware/Tools/cubemx_post_generate.ps1` | CubeMX 生成后一键修复 + 校验（含 4 个 USER CODE 关键调用检查） |

## 4. 构建 / 烧录 / 回归

```powershell
# 编译（必须 Rebuild，Build 会留旧 startup.o）
& 'D:\Program Files\Keil_v5\UV4\UV4.exe' -r Firmware\MDK-ARM\Compact_Induction_Heater.uvprojx -j0 -o build.log

# host 单测（mingw gcc）
python Firmware/Tests/host/run.sh          # 或按 README 逐个编译；7/7 通过

# PulseView 解码器回归（离线，无需 PulseView）
python Tools/pulseview_pd_decoder/test_decoder.py

# CubeMX 改配置之后（顺序不能变）
# 1) CubeMX Generate Code
# 2) powershell -ExecutionPolicy Bypass -File Firmware/Tools/cubemx_post_generate.ps1
# 3) Keil Rebuild
```

串口命令：

| 命令 | 作用 |
|---|---|
| `pd` | 打印 attached/contract/mode、bus/select/current/PDO、EPR enabled/mode/auto |
| `pd epr` | 手动进 EPR（绝对位置，已验证） |
| `pd epr1` | 用 1 基下标（本机会被 Hard Reset，仅作对照） |
| `pd auto` | 切换"接入后自动申请 EPR"（下次接入生效） |
| `pd epr` 之前建议先 `pd` 确认已在 20 V 合同 |

## 5. 未完成事项（按优先级）

0. **`pd off`（28 V EPR → 5 V）——已定位两级原因，改用"主循环直接发 Request"**。
   **第一级（旧代码）**：`pd off` 打出 `[CLI] Fixed request OFF (5V); negotiating`
   之后**再无任何串口输出、没有 `[BOOT]` 重启、OLED 停在最后一帧** —— 不是掉电，
   是**超级循环停了**（`printf` 只入 512 B 环形队列，真正发 DMA 的是主循环里的
   `DebugUART_Service()`；循环一停，队列里的字永远发不出去）。旧代码从主循环直接调
   `USBPD_PE_Request_EPRModeExit()`，和"从主循环调 `EPRModeEnter`"同一类错误。
   **第二级（本轮抓到的真因）**：改成"重取 SPR 能力、等 DPM 回调 Evaluate"后，
   日志显示 `EPR_ACT 690`（GetSrcCap 已发）→ `PD_TX a=7`（确实上总线）→
   `NOTIFY 13`(GETSRCCAP_ACCEPTED) → 两次 `SPR_CAPS` 到达 —— 但 **Evaluate 从没被调用**
   （没有 `EPR_ACT 693`/`EPR_ACT a=0 b=0xBAD0`/`REQUEST` 记录），所以我们一个 Request
   都没回。tSinkRequest 超时后源端发 **Hard Reset**；Sink 进入
   `HR_STATUS_START_ACK → WAIT_VBUS_VSAFE0V → FAILED`（这块板 VBUS 根本不掉到
   vSafe0V），然后**每秒重发一次 HR**，直到人工断电。现象与现场完全一致。
   **结论**：在 EPR 模式下，PE 收到 Source_Capabilities **不会**再向 app 要 RDO，
   所以 app 必须自己发 Request。
   **现实现**（`IH01A_EPR_EXIT_API_FALLBACK=0`，默认）：
   `PD_SendPolicyRequestDirect()` 由主循环直接构造并发送 Request（报文形态仍是
   EPR_Request：RDO + 复制的 Source PDO，因为 PE 处于 EPR 模式），
   目标按策略选：`!req_enabled` → 最高合法 Fixed **SPR PDO**（`pd off` → 5 V/3 A）；
   `req_enabled && cap>=28` → 缓存的 28 V EPR PDO（`pd on` 回 28 V）。
   **板端已验证（C140，2026-11-02）——这条链路现在是对的，别再改回去**：
   ```
   [CLI] Fixed request OFF (5V); negotiating
   EPR_ACT a=693 b=0x13880001     ← 目标 5000 mV / 位置 1
   EPR_ACT a=694 b=0x00000001     ← PE 返回 USBPD_OK，位置 1
   PD_TX   a=9  (EPR_Request)  do1=0x11C4B12C do2=0x0A81912C   ← 5V RDO + 5V PDO 副本
   NOTIFY  a=1  (REQUEST_ACCEPTED)
   CONTRACT a=1
   ```
   之后 `pd on` → `EPR_ACT 693 b=0x6D600008`（28000 mV / 位置 8）→ 同形态 EPR_Request
   `do2=0x0008C1F4` → 28 V 回来。`pd max 15` → `b=0x3A980004`（15 V / 位置 4），
   `pd max 20` → `b=0x4E200005`（20 V / 位置 5，副本 `0x000641F4`），全部 CONTRACT 成功。
   **重要结论：全程没有退出 EPR 模式**。5 V 合同期间 ST 栈继续按 500 ms 发
   EPR KeepAlive（`PD_TX a=16 do1=0x00038002`），C140 每次都 GoodCRC，会话保持，
   所以"降到 5 V"不需要 EPR_Mode Exit —— PD3.1 允许 EPR 模式下针对 SPR PDO 发
   EPR_Request（DO2 = 该 SPR PDO 的副本）。
   `pd max N` 在 `pd off` 状态下只保存不协商（请求仍是 5 V），CLI 现在会明确打印
   `stored; request is OFF (5V), applies after 'pd on'`，串口另有 `MANUAL a=3 b=<V>`。
   两个取证手段（保留）：
   - `PD_Diag_Flush()` 会自己踢一次 `DebugUART_Service()`，所以关键路径的记录在
     "下一句就卡死"的情况下也能真正出现在串口上；
   - `HardFault`/`NMI` 用**直写 USART1（关 DMA、轮询 TDR）**打印
     `[FAULT] … sp=… off=0/8/16 pc=… lr=…`：PC 落在 0x0800xxxx 的那一行才是真的
     异常栈帧，可直接在 .map 里查函数名。没有 `[FAULT]` 但串口彻底静默 ⇒ 死循环。
   - `HARD_RESET a=<n>` 的 n 是 `USBPD_HR_STATUS_*`：0 START_ACK / 1 START_REQ /
     2 MSG_SENT / 3 WAIT_VBUS_VSAFE0V / 4 WAIT_VBUS_VSAFE5V / 5 COMPLETED / 6 FAILED。
     `NOTIFY a=30`=HARDRESET_RX、`a=31`=HARDRESET_TX、`a=13`=GETSRCCAP_ACCEPTED。
   - **坑**：一旦出现"能力集无人应答"→ 源端 Hard Reset → Sink 等 vSafe0V 等不到 →
     `HR_FAILED` 每秒重试，只能人工断电。所以要避免任何"发了 GetSrcCap 却不回
     Request"的路径（EPR 模式内尤其）。

1. **375 ms KeepAlive —— 已验证不可行，保持 500 ms**。板端逻辑分析仪（4 MSa/s）实测：
   最小间隔仍是 **500 ms**，没有出现任何 375 ms 间隔、也没有交错。结论：应用通过
   `USBPD_PE_Send_ExtendeControlMessage(EPR_KEEPALIVE)` 发出的请求**没有被 PE 真正发送**
   —— 与 EPR 入口同一类问题（从主循环登记的 AMS 不会被 PE 执行）。
   **本次交接修复**：编译期宏 `IH01A_EPR_APP_KEEPALIVE_PROBE=0`（默认）禁止
   应用发送额外 KeepAlive AMS；只让 ST 栈负责已验证的 500 ms 帧。
   如需重新诊断，设为 1 后最多执行 **一次 API 接受的探针**（`EPR_ACT a=683`/
   0x2AB，不能据此推断线上真的发帧），或者连续 **两次 API 拒绝**（`EPR_ACT
   a=682`/0x2AA，`b` 为原始返回码）便停用，避免 API 返回 OK 却没有上总线时
   无限反复登记 AMS。**500 ms 在当前 250–500 ms KeepAlive 窗口内**。
   此次仅做代码审查与静态核验，未在本环境重新 Keil Rebuild/烧录。
   若将来确实需要 375 ms，可行路线只有三条，按风险从低到高：
   (a) 找到 PE 回调上下文（例如某条通知回调里）来登记这个 AMS；
   (b) 改 vendored 库里 `_Generic_AMS_Send_EPRKeepAlive_SNK` 的 timing 表（在 .lib 里，
       需要 ST 源码或自行打补丁）；
   (c) 在 PHY 层自行注入完整帧（需要自己做 CRC 与 PRL 状态同步，风险高，
       可能破坏已验证 6 h 的稳定性）。
2. **硬件闸门**（软件已就绪，但不做这些不许带载）：独立 OVP、28 V/5 A + 瞬态输入
   通路复核、240 W（50 V/5 A）e-mark 线、首次 28 V 必须空载 + 限流源。
   加热 PWM 目前保持关闭。
3. **合并回 `main` 前的清理**：
   - 诊断（`PD_TX/TXDO/RX/GETINFO/SETINFO`）建议收进一个 `IH01A_PD_DIAG_*` 宏；
   - `IH01A_PD_FORCE_SINK_TXOK` 的注释要保留（这是本板 CC 拓扑的必需补偿，不是临时 hack）；
   - `IH01A_EPR_28V5A_HARDWARE_APPROVED=1` 目前是默认值，合并前确认这是量产意图；
   - `SinkOperationalPDP=140 W` 与实际请求（当前 5 A）保持一致；若要改 3 A 策略，
     需同时改 `IH01A_EPR_SINK_PDP_W`、`IH01A_EPR_REQUEST_MA`、Sink PDO 20 V 档电流。
4. **未使用的源端能力**：本适配器还提供 PPS 5–21 V 与 EPR AVS 15–28 V，目前都不用
   （只走 Fixed SPD/EPR）。若以后要用 AVS，`pd_select.c` 与 `pd_input.c` 需要新增 APDO 路径。
5. **EPR 失败回落策略**：失败即退回 SPR 且不自动重试（每次接入一次）。若产品需要
   "插上就一定要 28 V"，需要加跨复位记忆或用户可配置重试。

### 5.1 已做过的 lib 反汇编结论（2026-11-02，别重复劳动）

对 `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib`（140,892 B，14 个成员）做了完整核查：

| 查了什么 | 结果 |
|---|---|
| KeepAlive 报文生成 | `usbpd_pe_extctrl.o` 的 `SendKeepAliveMessage`：只是 `USBPD_PE_FormatExtendedCtrlMsg(port, 3, 0, 0)` + 置 PE state 0x19，**不含任何周期** |
| KeepAlive 状态机 | `_Generic_AMS_Send_EPRKeepAlive_SNK`（5 B：AMS=0x19 + state table 指针）+ `_Generic_AMS_Send_EPRKeepAlive_SNK_table`（**39 B = 3 条 × 13 B**）。表项布局实测：`state@0 / state_next@1 / state_error@2 / timing@3..4（uint16 LE）/ ptrDataFunction@5..8（重定位在 0x05、0x12）/ action@9..12`；三条 timing = **28 / 0 / 0 ms**（28 ms 即 tSenderResponse），**没有 500** |
| 代码里的 500/375/480/400 | 全镜像反汇编 grep：`#0x1f4`(500) 命中 3 处均为 `LDR rX,[pc,#500]`（无关）；`#0x177`(375)、`#0x1e0`(480)、`#0x190`(400)、`#0x7d`(可选 500 编码) 均无相关命中 |
| 所有 14 个成员的数据段里的 0x01F4 / 0x0177 | **一次都没有**（覆盖 .rodata/.constdata/.data） |

**结论：500 ms 不是库里的常量，而是 PE 重新启动 KeepAlive 状态机的节奏**（所以实测会出现"最小 500 ms、部分间隔更大"的抖动特征）。**改 .lib 里任何常量都不会得到 375 ms**。

如果一定要 375 ms，只能走 §5 第 1 条列出的 (a)/(b)/(c) 三条路，其中最可控的是 (c) PHY 层注入：帧内容是
`9E 90 | 02 80 | 03 00 | CRC32`（Header `0x909E` = extended + NDO 1 + MsgID 7、ext header `0x8002` = 非分块 / 2 字节、ext control type 3 = KeepAlive；CRC 覆盖 header+ext header+全部字节，算法已在 77 帧真实抓包上 100% 验证）。
## 6. 关键坑（都踩过）

- **CubeMX 重新生成会删**：`Firmware/USBPD/**`、`_v541`、`stm32g0xx_ll_tim.c`、
  `uvprojx` 的 FULL define/库/include，以及 `main.c`/`stm32g0xx_it.c` 里**非 USER CODE**
  的关键调用（`DebugUART_Init`、`USBPD_DPM_Run`、`USBPD_DPM_TimerCounter`、
  `USBPD_PORT0_IRQHandler`）。所以这些都放在 USER CODE 里，并由 `cubemx_post_generate.ps1` 校验。
- **改任何编译选项后必须 Rebuild**（否则 `startup_stm32g071xx.o` 是旧的，会报
  `__initial_sp`/`__heap_base` 未定义）。
- **`main.c` 有生成护栏**：`USBPDCORE_LIB_PD3_FULL` 未定义时 `#error`，提示先跑修复脚本。
- **板子由 VBUS 供电**：源端发 Hard Reset 会让 MCU 掉电重启，所以任何"每次插入都
  硬复位"的行为都会表现为反复重启。正常流程不该有 Hard Reset。
- **本板上电时若 MCU 单独复位而源端仍持 20 V**，硬复位会卡在等 VBUS 降到 vSafe0V
  （`HARD_RESET a=3` → `a=6`）。拔插线即恢复；不是软件 bug 但值得记录。
- **PulseView 解码器**是 Python 3.4 运行库：不能用 f-string/walrus（见 §7）。

## 7. 逻辑分析仪 / 解码器（已就绪）

- 采样率 **4 MSa/s**（`pd decode.txt` 的时间戳是**采样计数**，不是 µs——换算要除以 4e6）。
- 解码器已升级到支持 PD3.1/3.2：EPR_Mode/EPR_Request、EPR 能力列表（含跨分块重组）、
  EPR AVS / SPR AVS、4 bit 对象位置、AVS RDO；扩展报文 CRC 覆盖"线上全部字节（含填充）"。
- 安装在 `D:\Program Files (x86)\sigrok\PulseView\share\libsigrokdecode\decoders\usb_power_delivery\pd.py`
  （原版备份 `pd.py.bak`），源码与回归测试在 `Tools/pulseview_pd_decoder/`。
- **重启 PulseView 才会加载**；`Warnings` 行出现 `CRC!` 时先查扩展报文长度/填充，
  不要先怀疑 CC 信号质量。

## 8. 诊断码字典（串口）

| 日志 | 含义 |
|---|---|
| `EPR_ENTER a=<status>` | `USBPD_PE_Request_EPRModeEnter()` 原始返回（0 OK / 1 不支持 / 2 ERROR / 3 BUSY） |
| `EPR_EVT a=<112..119>` | EPR 通知原始事件号，下一行是名字（ACK/SUCCEEDED/FAILED/…）；`a=0 b=0xD00D` = 投递后 1 s 内 PE 完全静默 |
| `EPR_ACT a=…` | 0x2A1 直接发 EPR_Mode 数据报文 / 0x2A2 重发 Request / 0x2A3 通知上下文登记 / 0x2A4 探针 GetSrcCap / 0x2A5 SinkTxOK 证据（`(CCx<<16)|vstate`）/ 0x2A6 延迟取 EPR 能力 / 0x2A9 缺 EPR 位时重取能力；**仅启用诊断探针时** 0x2AA=KeepAlive API 拒绝、0x2AB=API 接受但不代表发帧 |
| `EPR_ACT a=688` | 0x2B0 决策去调 `USBPD_PE_Request_EPRModeExit()`（仅在 `IH01A_EPR_EXIT_API_FALLBACK=1` 时出现） |
| `EPR_ACT a=685` | 0x2AD 上面那个 PE API 的原始返回 |
| `EPR_ACT a=689` | 0x2B1 策略变更后 2 s 的状态快照（一次性）：b 的 bit0 attached / bit1 显式合同 / bit2 EPR 模式 / bit3 待定 RDO / bit4 退出排队 / bit8..11 重试数 / bit12 请求使能 / bit16..23 maxFixed |
| `EPR_ACT a=690` | 0x2B2 EPR 内降档旧方案（重取 SPR 能力次数/状态）——已废弃，仅旧固件会打 |
| `EPR_ACT a=691` | 0x2B3 三次重取都没拿到能力集（旧方案，已废弃） |
| `EPR_ACT a=693` | 0x2B5 EPR 模式内直接发 Request 的目标 PDO（`mv<<16 | position`） |
| `EPR_ACT a=694` | 0x2B6 上面那次 `USBPD_PE_Send_Request()` 的原始返回（`status<<16 | position`） |
| `EPR_ACT a=695` | 0x2B7 连投 3 次仍未成功，停止并通过 0 W 预算保护（`selected_mv<<16 | position`） |
| `PD_TX a=<类型> b=<头<<16|长度>` | 每种报文首次出现（重复帧只记一次）；`a=0x10`=扩展，`a=9`=EPR_Request，`a=10`=EPR_Mode |
| `PD_TXDO do1/do2` | 该帧前两个数据对象（核对 RDO/CopyPDO 用） |
| `PD_RX a=<类型> b=<头>` | 收到的报文类型 |
| `GETINFO/SETINFO a=<DataId> b=<字节数>` | PE 向 DPM 取/送数据（`30`=SNK_PDP_EPR，`25`=SNK_PDO_EPR，`29`=REQ_COPYPDO，`26`=RCV_SRC_PDO_EPR） |

## 9. 参考文档

| 文档 | 内容 |
|---|---|
| `Docs/USBPD_FULL_EPR_IMPLEMENTATION_STATUS.md` | 完整演进记录：20 V 回归、EPR 静默排查、反汇编分析、逻辑分析仪解码、四个必要条件 |
| `Docs/HANDOFF_PD_EPR_FULL.md` | 更早的迁移交接（FULL 栈替换、设备层、时序坑） |
| `Docs/USBPD_FULL_EPR_MIGRATION_IMPACT.md` | uvprojx 改动清单、应用层 API 落差 |
| `Tools/pulseview_pd_decoder/README.md` | 解码器安装/回滚、改动清单、测试向量 |
| `Firmware/Middlewares/ST/STM32_USBPD_Library_v541/VERSION.md` | vendored 物料来源 |

## 10. 2026-11-02 Source PDO、SPR 回退与 KeepAlive 修改决策

- Statistics → **Source PDO** 已由 PD in 更名，分别保存 SPR 1..7 与
  EPR 8..13 位置，支持 EPR Fixed 和 EPR AVS（只展示，不发送 Request）。
  对于收到的 EPR-only 4..24 字节，内容映射到位置 8 开始；
  32..52 字节 combined 列表直接使用绝对位置。28 字节格式不明确，
  安全拒绝。SPR 刷新、EPR 退出/失败、Hard Reset 与 Detach 均失效化旧记录。
- **无 EPR / EPR 失败的 SPR 回退**：初次收到非 EPR Source Capabilities
  时，仍由 `PD_Input_BestFixed()` 选择不超过 20V 的最高合法 Fixed 电压。
  Hard Reset 后清除旧 `attempted/position/selected`，下一次重新选择最高
  Fixed SPR；保留 `s_epr_enter_once=1`，同一次物理插入不自动重进 EPR。
  之前的 SPR PDO 从新能力列表中消失时，也重新选最高合法 Fixed SPR，
  不无条件降至 5V。既有有效 SPR 合同仍保持。
- **ST KeepAlive 核心库**：当前用的 v5.4.1
  `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib` 的 EPR KeepAlive 调度在预编译
  策略引擎内部，仓库没有对应的完整可编辑 C 源。Git 管理可追踪修改，
  但不能自动使库内计时器变成可配置。**本次没有改动二进制 .lib**，
  仍为已实测 6 小时稳定的约 500ms。推荐先获取对应版 PE 源码或
  ST 正式支持的计时参数，再用独立实验分支修改、全量 Rebuild 与波形核验；
  没有唯一符号/代码路径核对和可重现补丁，不直接改库常量，也不绕过 PE
  从 PHY 注入 KeepAlive。
- **以后合并到 main**：功能分支验证后正常 PR 合并。如果未来确实
  修改 ST vendored 库，需同时合并上游版本、许可证、补丁源码/重现脚本、
  文件校验和、测试记录及改动过的库，而不是只同步 UI 或应用层定时。
- 新增 host 回归覆盖 SPR-only 15/20V 最高 Fixed 选择、EPR Fixed/AVS、
  EPR-only/combined 两类回调及失效化。**尚未在本机运行 Keil Rebuild
  或板端测试，不能把此次改动称为硬件验证通过**。

## 11. Fixed 请求及已生效合同接口（SW3538 7A 兼容）

- `PD_Input_SetRequestEnabled(0)`：Sink 保持启动、外接 Rd 不变，
  异步请求 5V Fixed。参数 1 恢复自动最高 Fixed 档。
- `PD_Input_SetMaxFixedVoltage(v)`：单位整数 V，5..31；
  按源端实际 Fixed PDO 查找不超过限制的最高电压；并非写死档位表。
  仅 28V EPR Fixed 当前支持；PPS/AVS 不参与档位请求。
  返回值：0=非法参数，1=已请求协商（或无需要），2=**只保存未协商**
  （`pd off` 状态下请求仍是 5 V，`pd on` 后才生效；CLI 会明确打印这一点）。
- `PD_Input_GetActiveContract(&contract)`：仅在 PE 通知显式合同、
  无策略切换、并且 VBUS 测量与电压目标相差不超过 10% 时返回有效。
  `contract.voltage_mv/current_ma/position/epr` 为已请求成功的
  Fixed PDO 电压与协议授权电流上限，**不是 ADC 实际输入电流**。
  不可用时返回 0 且将 out 清零，功率环不应继续使用旧电流预算。
- SW3538 20V/7A 非标 Fixed PDO 可以直接按 PDO 字段编码 7000mA
  的 SPR RDO；**不增加线缆确认流程，不针对 20V/7A 做特殊匹配**。
  当前 `IH01A_PD_SPR_MAX_MA=7000` 是本机硬件上限；
  20V/3A、20V/5A、20V/7A 分别按实际 PDO 电流请求，
  声明更高电流时限幅至 7A。28V EPR 请求仍为 5A。
- API 改变的是异步 PD 策略，不通过 Reset MCU 或关闭 CC Rd 来
  降压。28V→5V **不再需要 EPR Exit**：见 §5.0，EPR 模式下直接发
  针对 SPR PDO 的 EPR_Request 即可（C140 已验证 5 V/15 V/20 V/28 V 双向）。
- 串口回归：`pd off`、`pd on`、`pd max 15`、
  `pd max 31`、`pd`，其中 `ACTIVE` 表示已确认合同，
  `select` 仍仅表示拟请求/上次请求，不可用于加热功率上限。
- 此次未修改 500ms KeepAlive。仍需 Keil Rebuild、Host 单测及
  5/15/20V、20V/7A、28V EPR 的板端协商抓包。
