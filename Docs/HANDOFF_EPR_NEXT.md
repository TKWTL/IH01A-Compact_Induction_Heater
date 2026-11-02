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
  最近一次：`0 Error(s), 0 Warning(s)`，`Code=80,620 RO=4,200 RW=476 ZI=22,748`。
- host 单测 7/7 通过；PulseView 的 PD 解码器已升级到支持 PD3.1/3.2（见 §7）。

## 1. 板端已实测、可以当结论用的事实

| 项 | 数据 |
|---|---|
| PD 库 | vendored `STM32_USBPD_Library_v541`（core `aafa359`，FULL lib 140,892 B），`LIB_ID=0x30410000` 校验通过 |
| 内存 | `reqmem=4506`（2 口口径，`USBPD_PE_GetMemoryConsumption()`），microLIB 堆 8 KiB |
| SPR 合同 | `REQUEST 20 V/5 A`，RDO `0x51C7D1F4`（OPOS=5、no_suspend、unchunked、EPR capable） |
| EPR 合同 | `EPR_Request 28 V/5 A`，RDO `0x81C7D1F4`（**OPOS=8 绝对位置**），CopyPDO `0x0008C1F4` |
| 源端能力（本机适配器） | 9 个对象：5 V/3 A、9 V/3 A、12 V/3 A、15 V/3 A、20 V/5 A、PPS 5–21 V、空、**28 V/5 A**、**EPR AVS 15–28 V/140 W** |
| KeepAlive | 栈自身 500 ms（= 规范上限）；应用侧定速 **375 ms**（见 §2 最后一条，待抓包确认） |
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
- KeepAlive：栈 500 ms，应用在 `PD_Input_EPR_Poll()` 里按 **375 ms** 定速补发
  （仅在距上一帧 KeepAlive ≥250 ms 时才发，保证不违反规范 250 ms 下限）。

## 3. 代码地图

| 文件 | 职责 |
|---|---|
| `Firmware/APP/algorithm/pd_input.c` | SPR/EPR 状态机：能力解析、选档、RDO 编码、EPR 阶段推进、KeepAlive 定速、CLI 用的运行时开关 |
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

1. **375 ms KeepAlive —— 已验证不可行，保持 500 ms**。板端逻辑分析仪（4 MSa/s）实测：
   最小间隔仍是 **500 ms**，没有出现任何 375 ms 间隔、也没有交错。结论：应用通过
   `USBPD_PE_Send_ExtendeControlMessage(EPR_KEEPALIVE)` 发出的请求**没有被 PE 真正发送**
   —— 与 EPR 入口同一类问题（从主循环登记的 AMS 不会被 PE 执行）。
   代码现在的行为：请求结果会记录一次（`EPR_ACT a=682`/0x2AA 失败码、`a=683`/0x2AB
   "已接受但请用分析仪确认是否真的上网"）；连续两次失败后自动停用该 pacer，
   保留栈自身的 500 ms 帧（**500 ms 在 PD3.1 的 250–500 ms 内，合规**）。
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
| `EPR_ACT a=…` | 0x2A1 直接发 EPR_Mode 数据报文 / 0x2A2 重发 Request / 0x2A3 通知上下文登记 / 0x2A4 探针 GetSrcCap / 0x2A5 SinkTxOK 证据（`(CCx<<16)|vstate`）/ 0x2A6 延迟取 EPR 能力 / 0x2A9 缺 EPR 位时重取能力 |
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
