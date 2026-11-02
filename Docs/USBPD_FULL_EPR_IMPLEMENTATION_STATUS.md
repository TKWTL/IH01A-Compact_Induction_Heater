# IH01A FULL EPR 28V/5A 软件迁移状态

**分支**：`feature/pd-epr-28v5a`；基点 `main@668e541`。`main` 未被改动。

**重要**：本分支已替换编译配置并接入 EPR 请求代码；**Keil armclang 已实测 0 error / 0 warning**（见文末 2026-11-02 记录），20V SPR 已板端回归通过。**EPR 28V 尚未在板端验证**，禁止作为生产固件。

## 交接 §4 的实际验收

| 步骤 | 代码/静态验收 | Keil/板端验收 |
|---|---|---|
| 1：UCPD 端口配置 | 完成：Port 0→UCPD2；RX DMA1 CH6，TX CH7；与 `Core/Src/ucpd.c` 一致。交接原文的 CH2/CH4 错误已纠正 | 不涉及编译 |
| 2：新版 DPM Core | 完成：拷贝 ST core `aafa359` 的 `src/usbpd_dpm_core.c`；增加 `USBPD_DPM_RequestWhatToDo` 回调 | 与步骤3成对构建，单独不兼容旧 MINSNK |
| 3：Keil FULL 切换 | 完成：宏、FULL CM0+ Keil lib、v541 inc、trace 和七个 G0 device .c 路径均切换；旧 MINSNK 路径已不再参与 Target | **待 Rebuild All Target Files**，检查 0 Error/Warning、Flash/RAM 使用量与 `[PD_INIT]` |
| 4：20V 回归 | 未改变正常 SPR 3A 选档。EPR 硬件许可开关默认 0，确保不会主动进入 EPR | **待板端实测 20V**（SPR_CAPS → REQUEST → ACCEPT → CONTRACT，ADC≈20V，OLED/串口稳定） |
| 5：EPR 试入 | 使用 ST `USBPD_SNKRDO_TypeDef`，不手写 RDO 位；代码已连接 EPR Mode Enter/Exit、EPR Caps、28V/5A 选档和 CopyPDO、失败恢复 | **待硬件检查通过、打开许可开关后，Keil Rebuild 与 PD 分析仪验证** |

## 必须执行的本地验证

1. 关闭 Keil 并同步 `feature/pd-epr-28v5a`；保持 `main` 完整。
2. Keil `Project → Rebuild All Target Files`（任何 MicroLIB/Define/IncludePath/库变化不能只 Build）。
3. 记录实际 `Program Size: Code/RO-data/RW-data/ZI-data`。芯片 Flash 128KiB、SRAM 36KiB；不能用 FULL .lib 档案大小替代链接实际占用。
4. 未授权 28V 时，检查 `U1OK`、`[PD_INIT] FULL v5.4.1 initialized`、`SPR_CAPS`、`REQUEST`、`CONTRACT`。串口发 `pd`，应看到 `EPR enabled=0 mode=0`，ADC 约 20V。
5. 若 `[PD_INIT] ... FAILED`，查 `g_usbpd_full_stage`（`0xE1` = Core/Library check；`0xE2` = User init；`0xE3` = InitOS）。
6. 只有经实物检查输入通路 28V/5A+瞬态、OVP/OCP、EPR 240W 5A e-marker 线缆、外部供电源及负载安全之后，才可将 `Firmware/APP/algorithm/pd_epr_policy.h` 中 `IH01A_EPR_28V5A_HARDWARE_APPROVED` 从 0 改为 1、**Rebuild All Target Files**、先无负载上电。
7. 使用 PD 分析仪确认 FULL 的 EPR Enter ACK/SUCCEEDED、Extended EPR Source Capabilities、28V Fixed EPR Request **两个对象：RDO + CopyPDO**、Accept/PS_RDY 和 KeepAlive。特别核实 `USBPD_CORE_DATATYPE_REQ_COPYPDO` 在 v5.4.1 Keil 库中的 `Size` 语义。通过这些前不得称 EPR 成功。
8. 无 28V/5A Fixed PDO、EPR 被拒绝或协议错误时保持/退回 SPR ≤20V，禁止自动重复 EPR 进入。手动 Retry 为唯一用户重试入口。
9. 加热 PWM 始终保持关闭，直到后续独立的硬件安全和测量验收。

## 本次代码组织与重要约束

- FULL Core/Device 物料来自 `Firmware/Middlewares/ST/STM32_USBPD_Library_v541`，旧目录保留只用于回滚，不在新编译路径中混用。
- `usbpd_dpm_core.c` 使用 ST FULL 版本；`usbpd_dpm_user.c` 与 `pd_input.c` 提供 EPR 回调、请求和状态。
- `pd_select.c` 只进行纯 PDO 选择；请求编码在 `pd_input.c` 内通过 ST 结构体完成，已删除旧的手写 EPR RDO 逻辑。
- `usbpd_devices_conf.h` 使用 **UCPD2 作为逻辑 Port 0**（RX DMA1 CH6、TX CH7），不使用新模板的双端口 `INSTANCE1` CH2/CH4：后者与 ADC CH2 和 UART RX CH4 冲突。
- 源文件 `usbpd.c` 初始化阶段可从串口与 Keil Watch 观察，便于排查库 ID、配置和 DPM 启动。
- `*.uvprojx` 和 `*.uvoptx` 均沿用项目最新 Keil 原生格式且继续跟踪；本次只修改 `uvprojx` 的语义配置，不改 `uvoptx`。
- 尚未确认当前 Core/Device/应用层组合的 ArmClang 链接及完整 EPR 报文行为；请不要直接合并主线。

## 2026-11-02 FULL 20V 板端回归结果与 EPR 实测方案

### FULL 20V 已验证（用户板端 UART，独立断电重启）

```text
[PD_INIT] LIB_ID=0x30410000 check=1 reqmem=4506 heap=8192
[PD_INIT] FULL v5.4.1 initialized
[PD 594] SPR_CAPS a=6 b=0x0A81912C
[PD 597] REQUEST a=20000 b=0x5104B12C
[PD 602] NOTIFY a=1 b=0x00000000
[PD 859] CONTRACT a=1 b=0x00000010
[CLI] bus=20048 mV select=20000 mV current=3000 mA PDO=5
```

- FULL 栈 20V/3A SPR 回归通过；供电测量与协商一致。4506B 是 PD 库报告的动态内存需求，8192B 是编译堆大小；不是已测量的堆峰值。
- 某次上电发生了多轮 HARD_RESET、再次断电后恢复正常。因为供电期间有人工断电，不能拼接不同启动的协商事件，继续留意冷启动电源时序。
- 用户已在 `Firmware/Compact_Induction_Heater.ioc` 关闭 CubeMX 的 USBPD 中间件勾选，USBPD 源码/库改为 Git 独立管理。`main` 中的 `.ioc` 仍需在后续合并前同步，但禁止 CubeMX 重新覆盖 vendored v541 文件。

### 28V 试验固件（当前 feature 分支）

- 用户确认输入硬件路径可以承受 28V。将 `IH01A_EPR_28V5A_HARDWARE_APPROVED=1`，但 `IH01A_EPR_28V5A_AUTO_ENTER=0`。**普通插电只会自动请求 SPR ≤20V；不会主动跳 28V。**
- 使用证实支持 28V/5A EPR 的 Source 和 240W（50V/5A）电子标记 USB-C 线缆，确认独立过压/过流保护；28V 首测空载、PWM 禁用、示波器监测 VBUS。
- EPR Source PDO 以独立数组交付：`USBPD_CORE_DATATYPE_RCV_SRC_PDO_EPR` 预期 4..24 字节（1..6 对象），映射到逻辑 PDO 8..13；不能把 4 字节当成完整 32 字节的合并数组。诊断 `EPR_CAPS a=原始字节数 b=首个 EPR PDO`。
  - `a` 的判读：**`a ≤ 24` → 库只交付 EPR 对象**（本次预期）；**`a ≥ 28` → 库交付的是 SPR+EPR 合并列表**。两种布局解析器都能吃：合并布局下对象索引本身就是位置（7 个 SPR 在前），EPR 部分从位置 8 起，结果一致。
  - 解析器只在 `a` 的取值上分支，**不会**推测缺失对象；`a=0` 或奇数字节直接丢弃并记录。
- EPR_REQUEST 按 FULL DPM 回调传入 **8 字节**，内容是 ST 类型化 RDO + 匹配的 CopyPDO；日志分别打印 `EPR_REQUEST` 和 `EPR_COPY`。若回调只收到 4 字节，考虑 ST EPR 分块重组缺陷，先分析协议仪而非手工拼造缺失 PDO。
- 没有可用的 28V/5A 固定 PDO、请求被拒绝或等待能力超时 2.5 秒，退出 EPR 返回 SPR，不自动重新进入。允许 CLI 或 UI 明确人工发起下一次尝试。

**必须 Keil Rebuild All Target Files**。先等正常 SPR 20V 进入稳定 Contract，再在串口发送命令：

```text
pd
pd epr
pd
uart
```

预期观察以下里程碑：

```text
[CLI] EPR enter queued ...
[PD ...] EPR_ENTER a=1
[PD ...] EPR_MODE a=1 b=0x00000072    (114: EPRMODE_SUCCEEDED)
[PD ...] EPR_CAPS a=4..24 b=...
[PD ...] EPR_REQUEST a=28000 b=...
[PD ...] EPR_COPY a=8..13 b=...
[PD ...] CONTRACT ...
[CLI] bus≈28000 mV select=28000 mV current=5000 mA
```

**注意**：EPR 状态日志等于协议回调，不直接证明 VBUS 实际电压；`current=5000` 是协商额度，不是电流实测。若 Source 仅提供 AVS、不提供 Fixed 28V/5A，此版本会保留 SPR，暂不实现 AVS。

异常定位：
- `pd epr` 被拒绝：检查前置 SPR contract、Source 5V Fixed PDO 的 EPR capable 标记和线缆。
- `EPR_ENTER a=0`：FULL API 在当前 Policy Engine 状态拒绝进入，检查触发时间和 EPR Sink 配置。
- `EPR_MODE` 失败：优先查看 PD 分析仪里的 cable capability/VCONN/EPR Mode Failed 原因。
- `EPR_CAPS a=4`：库可能只向应用层提供 1 个 EPR PDO；判断收到的原始 PDO 是否就是 Fixed 28V/5A，不能推测其余对象存在。
- `EPR_CAPS a=0` 或 `EPR_MODE b=0xBAD1`：收不到完整有效能力，2.5s 超时退出。
- `EPR_REQUEST` 后无成功 Contract：抓取 EPR_REQUEST 两个 Data Object 和 Source Accept/Reject/Wait，核对 CopyPDO 是否与 EPR Source PDO 完全一致。
- 仍有波形异常、输入过压或加热器侧保护异常时立即停止测试，不应提高负载。

### 本次离线验证

在当前执行环境，使用主机 C 编译器和 AddressSanitizer/UndefinedBehaviorSanitizer 对独立复现的 `pd_select.c` 选择逻辑运行了 SPR 固定 20V、单个 4B EPR Fixed 28V/5A、AVS 忽略、30V/36V 排除测试，全部通过。**这不是 Keil 真正 Rebuild，也不能替代板端 EPR 协议验收。**

### 2026-11-02 Keil Rebuild + EPR 能力解析加固（交接机复测）

- `Keil UV4 -r` Rebuild All Target Files：**0 Error(s), 0 Warning(s)**，`Code=78,312 RO-data=3,916 RW-data=464 ZI-data=21,120`（Flash 128 KiB 用 82,228 B ≈ 63%）。
- host 单测 7/7 通过（`pm_timer` / `mini_format` / `pd_source_stats` / `time` / `pd_select` / `ntc_lut` / `temp_service`，mingw gcc 8.1 `-Wall -Wextra -Werror`）。
- `PD_Input_EPRCaps()` 增加**布局自适应**：`a ≤ 24` 按 EPR-only 存放到 `s_epr_caps[7..12]`（首对象=位置 8），`a ≥ 28` 按合并布局存放到 `s_epr_caps[0..]`；上限校验放宽到 `(USBPD_MAX_NB_PDO+USBPD_MAX_NB_EPRPDO)*4 = 52` 字节。这样 ST 到底交付哪种布局都不会把对象位置整体错位，也不会因为 4 字节载荷静默失败。
- `EPR_CAPS` 的 `b` 改为**首个 EPR PDO**（位置 8），两种布局下都是同一个对象，便于直接核对是不是 `0x...` 的 28V Fixed。
- 保留了「必须 28V **且** 5A」的严格条件：28V/3A 的适配器会被判为不合格并退回 SPR（`EPR_MODE b=0xBAD0`），这是本产品 140W 目标的刻意取舍。

### 2026-11-02 EPR 进入失败的定位手段（本轮新增）

板端第一份 EPR 日志只有 `EPR_ENTER a=1`，随后没有 `EPR_MODE` / `EPR_CAPS` / `EPR_REQUEST`。旧代码把返回值压成了布尔，**无法区分「API 已提交」与「协议层被拒/未发出」**，所以本轮先把所有观测点补齐，再谈 28 V。

**A（EPR Sink PDP）原本就已经有了**，不是缺失项：

| 回调 | 现状 |
|---|---|
| `USBPD_CORE_DATATYPE_SNK_PDP_EPR` | 返回 1 字节，值 `IH01A_EPR_SINK_PDP_W`（140 W），非 0 |
| `USBPD_CORE_DATATYPE_SNK_PDO_EPR` | 返回 32 字节：Sink PDO 1..5（SPR）+ 位置 8 = 28 V/5 A Fixed |
| `USBPD_CORE_DATATYPE_SRC_PDO_EPR` | 新增：`*Size=0`（本口只做 Sink，避免 PE 读到未初始化长度） |

PDP 用 140 W 而**不是** 84 W：Sink PDO 与请求电流都是 5 A，PDP 写 84 W 会与 5 A 能力自相矛盾，Source 有权据此拒绝 EPR。要切 3 A 策略必须同时改三处（`pd_epr_policy.h` 的 `IH01A_EPR_SINK_PDP_W` / `IH01A_EPR_SINK_CURRENT_MA`，以及 `pd_input.c` 里 `mv==28000u && ma==5000u` 的判定）。

**B：`EPR_ENTER` 改为记录原始返回码**

| 值 | 含义 |
|---|---|
| 0 | `USBPD_OK` — 请求已提交给 PE（**不等于报文已发出**） |
| 1 | `USBPD_NOTSUPPORTED` |
| 2 | `USBPD_ERROR` — 当前状态不允许 |
| 3 | `USBPD_BUSY` — 有操作在进行 |

**C：所有 EPR 通知都进日志**（新增 `EPR_EVT`，`a` = 原始事件号，`b` = 状态位 bit0 `epr_mode` / bit1 `copy_valid` / bit2 `enter_once`），且紧跟一行事件名：

| a | 事件 | 意义 |
|---|---|---|
| 112 | `EPRMODE_INVALID` | 请求无效（线缆/VCONN/能力不足） |
| 113 | `EPRMODE_ACK` | Source 已回 GoodCRC + EPR_Mode ACK |
| 114 | `EPRMODE_SUCCEEDED` | EPR 模式进入成功 |
| 115 | `EPRMODE_FAILED` | Source 拒绝 |
| 116 | `EPRMODE_INIT` | 模式初始化 |
| 117 | `EPRMODE_EXIT` | 退出 EPR |
| 118 / 119 | `EPR_SNKCAP/SRCCAP_RECEIVED` | 能力消息到达 |

**D：`EPR_CAPS` 已经记录原始字节数**（`a`），配合本轮新增的布局自适应即可判断 ST 交付的是 EPR-only（≤24 B）还是合并（≥28 B）。

**E：新增 `EPR_ACT`，记录 PE 向 DPM 的每次提问和我们的回答**

| a | 动作 | b |
|---|---|---|
| 4 | `USBPD_ACTION_CHECK_PDO` | 0（`USBPD_OK`，头文件规定这是唯一合法回答，否则 PE 会发 Hard Reset） |
| 1 / 2 / 3 | `ENTER_USB` / `DATA_RESET` / `ENTER_MODE` | 1（`USBPD_NOTSUPPORTED`） |
| 481 | 查 `SNK_PDP_EPR` | 返回字节数（1） |
| 482 | 查 `SNK_PDO_EPR` | 返回字节数（32） |

**下一次日志怎么读**

| 现象 | 结论 |
|---|---|
| `EPR_ENTER a=0` + `EPR_EVT a=113` | EPR_Mode 报文确实发出并被 Source ACK，接下来只该等 114/119 |
| `EPR_ENTER a=0` + 无任何 `EPR_EVT` | 报文没出去或 PE 未发：用 CC 分析仪确认；同时看 `EPR_ACT` 是否有 CHECK_PDO |
| `EPR_ENTER a=1/2/3` | 应用层调用时机或 PE 状态问题（0/1/2/3 见上表） |
| `EPR_EVT a=112` 或 `115` | Source 明确拒绝：先查 e-marker 线缆、VCONN、Sink EPR 能力声明 |
| `EPR_EVT a=119` 之后才有 `EPR_CAPS` | 能力消息到了但对象没被识别：看 `EPR_CAPS a` 字节数与 `b` 首个 PDO |
| `EPR_ACT a=4 b=0` 反复出现 | PE 在反复做 PDO 校验，说明流程卡在 PDO 有效性而非 EPR 模式本身 |

本轮 Rebuild：**0 Error(s), 0 Warning(s)**，`Code=78,676 RO-data=3,988 RW-data=468 ZI-data=21,124`；host 单测 7/7 通过。`EPR_ACT` 的 481/482 只在值变化时记录，避免 PE 轮询把 32 条诊断队列冲掉。

### 2026-11-02 板端日志判读（EPR_Mode 未发出）+ 观测面再扩一圈

**`U1OK` 是什么**：`DebugUART_Init()` 在 `MX_USART1_UART_Init()` 之后用 **阻塞 HAL_UART_Transmit** 发的 6 字节探针（`Firmware/Core/Src/usart.c:251`），早于所有 printf 和调度器。它证明"已烧录 + USART1 初始化跑到了 + PB6 真的在发"。因此日志里 `U1OK` 出现在上一段的末尾时，它属于**下一段启动**：`U1OK\r\n` 后面紧跟 `[BOOT]` 自带的 `\r\n`，看起来就是空行。

**`flags=` 是 `RCC->CSR` 原始值**（`main.c:114` 在 HAL 初始化前抓取，且**从不清除**，所以标志会累积）：

| 位 | 含义 |
|---|---|
| 26 `PINRSTF` | NRST 引脚复位（调试器 reset / 复位键） |
| 27 `PWRRSTF` | 上电/掉电复位 |
| 28 `SFTRSTF` | 软件复位 |
| 29/30/31 | IWDG / WWDG / 低功耗复位 |

- `0x04000000` = 只有 PINRST：**上次是调试器/复位键复位，MCU 单独重启而 VBUS 仍有 20 V**。
- `0x0C000000` = PINRST + PWRRST：随后又发生过掉电复位（拔插线），即用户手动断电重启那一次。

**`HARD_RESET a=` 是 `USBPD_HR_Status_TypeDef`**：0 `START_ACK`、1 `START_REQ`、2 `MSG_SENT`、3 `WAIT_VBUS_VSAFE0V`、4 `WAIT_VBUS_VSAFE5V`、5 `COMPLETED`、6 `FAILED`。
`[PD 544] HARD_RESET a=1` → `a=3` → `[PD 1544] a=6` 的意思是：**硬复位发出去了，卡在等 VBUS 降到 vSafe0V，最后 FAILED**。也就是 Source 没有把 VBUS 拉下来（或我方放电不足/ADC 采不到），硬复位流程走不完；这也是"带电复位后一直起不来、必须拔插"的机制，不是随机故障。`[PD ...] NOTIFY a=31` 正好是 `USBPD_NOTIFY_HARDRESET_TX`（我们这边发出的硬复位）。

**本次 EPR 的关键结论**：`EPR_ENTER a=0`（`USBPD_OK`，请求已投递给栈）之后 **718/4823 ms 内一条 `EPR_EVT` 都没有**，也没有任何 `EPR_ACT`，随后第二次 `pd epr` 返回 `a=3`（`USBPD_BUSY`）。这说明：栈**收下了**请求并把它标记为"进行中"，但 PE **既没有向我们索取 PDP/EPR PDO，也没有发出任何 EPR 通知**。所以此刻不是线缆/PDO/PDP 的问题，而是"PE 没开始干活"或"开始前问的东西我们没答对"。

为此本轮再补三项观测（Rebuild：0 error / 0 warning，`Code=78,848 RO-data=4,000 RW-data=468 ZI-data=21,180`）：

1. **`GETINFO` / `SETINFO`**：PE 每一次向 DPM 取/送数据都记录 `DataId` 与字节数，**每个 id 只记第一次**（`PD_SeenOnce`，4×32 位位图），避免 PE 轮询冲掉 32 条队列。这条最可能直接给出答案：如果 PE 在"忙"的时候向我们要了某个 DataId（例如 `USBPD_CORE_DATATYPE_EPRMODE`）而我们返回了 0 字节，就能立刻看出来。
2. **`GetDataInfo` 的 default 分支现在写 `*Size=0`**：以前不写，PE 可能读到栈上残留长度并据此继续处理，这是"静默卡住"的典型来源。
3. **`EPR_EVT a=0 b=0x0000D00D`**：请求已投递但 1 s 内没有任何 EPR 通知时，主动记一条"PE 静默"标记，免得日志看起来像"什么都没发生"。

下一份日志需要看的新行（`DataId` 取值：`SNK_EXTENDED_CAPA`=19、`ENTERUSB`=20、`EPRMODE`=23、`SNK_PDO_EPR`=25、`RCV_SRC_PDO_EPR`=26、`REQ_COPYPDO`=29、`SNK_PDP_EPR`=30；`PECABLE` 未启用，故无 20/21 两个线缆身份项）：

```
[PD n] GETINFO a=30 b=0x00000001     ← PE 要 EPR Sink PDP，返回 1 字节(140)
[PD n] GETINFO a=25 b=0x00000020     ← PE 要 EPR Sink PDO，返回 32 字节
[PD n] SETINFO a=26 b=0x00000004     ← PE 交来收到的 Source EPR PDO
[PD n] EPR_EVT a=0 b=0x0000D00D      ← 请求已投递但 1s 内 PE 完全静默
```

### 2026-11-02 板端复测：PE 收下请求但完全静默（GETINFO 证据）

冷启动日志（新固件）：

```
[PD 431] SETINFO a=2 b=0x00000004    RDO_POSITION
[PD 431] SETINFO a=6 b=0x00000004    RCV_REQ_PDO
[PD 594] SPR_CAPS a=6 b=0x0A81912C
[PD 594] SETINFO a=4 b=0x00000018    RCV_SRC_PDO, 24 B = 6 个 PDO
[PD 602] NOTIFY a=90                 POWER_STATE_CHANGE
[PD 858] NOTIFY a=16 / a=32          显式合同 / STATE_SNK_READY
[PD 1538] NOTIFY a=120               TAME 超时（tAMETimeout = 1100 ms）
[PD 7114] EPR_ENTER a=0              USBPD_OK（请求已投递）
[PD 7819] EPR_ENTER a=3              USBPD_BUSY（700 ms 后仍"进行中"）
[PD 8820] EPR_EVT a=0 b=0x0000D00D   PE 静默 1 s
```

**关键证据：整段日志没有任何 `GETINFO a=30/25`。** 也就是说 PE 收下 EPR 请求后**从未向应用索取 EPR Sink PDP / EPR Sink PDO**——不是"我们给的数据不对"，而是"PE 根本没走到构造 EPR_Mode 那一步"。同时也没有任何 `EPR_EVT`。`NOTIFY a=120` 是 `USBPD_NOTIFY_TAMETIMEOUT`（Alternate Mode Entry，1100 ms），属于 SPR 阶段的 SVDM 超时，与 EPR 无直接关系，但说明对端有过 VDM 交互。

### 2026-11-02 与 CH32X035 参照项目的差异（同作者已量产验证的 28 V 基线）

参照：`E:\Embedded_Full_ENG_Path\Desktop_PD_Power_Converter\Firmware\Peripheral\PD\pd_snk_debug.md`（以及 `CH32X035_DemoBoard` 同结构）。**它是 CH32X035 自研 PHY + 自研策略层，不是 ST FULL 栈，所以不能直接验证 ST 的内部行为**；但它是"源端期待什么样的 Sink 行为"的实测基线：

| 事项 | 参照实现（稳定进 28 V） | 本项目改动前 | 处理 |
|---|---|---|---|
| 首个 SPR Request | 20 V / **5 A**、PDO5、EPR-capable（帧 `82 10 F4 D1 47 51`，RDO `0x5147D1F4`） | 20 V / **3 A**、EPR-capable（`0x5144B12C`） | **已改**：`IH01A_PD_SPR_MAX_MA=5000`，Sink PDO5 = 20 V/5 A；3 A 线缆时源端 PDO 只给 3 A，请求自动仍是 3 A |
| EPR 入口前 | 先发 `Get_Source_Cap_Extended`，按源端 PDP 自适应 Enter（140 W 兜底，上限 224 W） | 固定 140 W（`SNK_PDP_EPR`） | ST PE 不暴露该消息的发送接口；先看 ST 自己会不会发 |
| Enter 被拒 | PDP>140 W 时 120 ms 后以 140 W 重试一次 | 不自动重试，只允许手动 `pd epr` | 保持（安全优先），待 EPR 能进再谈 |
| 时序 | Get 延迟 120 ms、caps 超时 500 ms、Enter 超时 550 ms、KeepAlive 375 ms/ACK 100 ms | ST 栈内部管理；我们只有 2.5 s 能力超时 | 观测中 |
| sender-response 关键路径 | 禁止 printf/阻塞/毫秒延时 | diag 是队列，只在主循环 `PD_Diag_Service()` 打印；PD 回调里无阻塞 | 已符合 |
| 每次接入只试一次 EPR | 是（`PD_EPR_FailedForAttach`） | 是（`s_epr_enter_once`） | 已符合 |
| Hard Reset | VBUS 供电板不主动发 HR，避免自己掉电 | SPR 阶段复位后 PE 会发 HR，且等 vSafe0V | 见上文 §硬复位：`WAIT_VBUS_VSAFE0V → FAILED` 是带电复位场景的死结 |

本轮 Rebuild：**0 Error(s), 0 Warning(s)**，`Code=78,848 RO=4,000 RW-data=468 ZI=21,180`（策略改动只换常量，代码尺寸不变）；host 单测 7/7 通过（`test_pd_select.c` 新增 20 V/3 A 与 20 V/5 A 两个钳位断言）。

### 2026-11-02 根因候选：PE 运行时特性位把 EPR 关掉了（`PE_UnchunkSupport = 0`）

同一套适配器 + 线缆在 CH32X035 上能稳定握手 28 V，说明源端、线缆、电源路径都没问题，**问题在本项目的 ST FULL 集成**。逐层排查后，DPM Core 与 ST 官方 `aafa359` **逐字节一致**（`src/usbpd_dpm_core.c` 只有我们加的诊断三处差异），所以问题落在 PE 的**运行时特性位**上。

`USBPD_SettingsTypeDef`（`usbpd_def.h:3580`）里 EPR 相关的开关有三个，之前只开了最后一个：

| 字段 | 改动前 | 说明 |
|---|---|---|
| `.PE_UnchunkSupport` | **FALSE** | Unchunked Extended Message 支持。EPR_Mode 是扩展控制消息、EPR_Source_Capabilities 是扩展消息，**PD3.1 要求 EPR 端口必须支持 unchunked** |
| `.Is_SrcCapaExt_Supported` | **FALSE** | `Source_Capabilities_Extended`（SPR 阶段报告源端 PDP/EPR 能力的那条消息） |
| `.Is_EPR_Supported_SNK` | TRUE（`..._HARDWARE_APPROVED`） | 头文件原话："EPR support need to use a library supporting EPR else it has no effect" |

这正是"`USBPD_PE_Request_EPRModeEnter()` 返回 `USBPD_OK`、随后 PE 既不索取 `SNK_PDP_EPR` 也不发任何 EPR 通知"的合理机制：PE 认为本端口不具备 EPR 所需的扩展消息能力，于是把请求挂起（`BUSY`）而不构造报文。

现已改为随 `IH01A_EPR_28V5A_HARDWARE_APPROVED` 一起打开，并在启动日志里把三个运行时位直接打出来（不是编译期宏，而是 PE 实际会看到的设置）：

```
[PD_INIT] PE cfg pd3=0x03xx unchunk=1 scrCapaExt=1 eprSnk=1
```

验收顺序：先确认 `REQUEST a=20000 b=0x5147D1F4`（20 V/5 A，SPR 未受影响），再 `pd epr` 看是否出现 `GETINFO a=30/25` 与 `EPR_EVT a=116/113/114`。

Rebuild：0 Error / 0 Warning，`Code=78,884 RO=4,068 RW=468 ZI=21,180`。

**板端结果：`PE cfg pd3=0x0809 unchunk=1 scrCapaExt=1 eprSnk=1` 全部生效，但 EPR 仍然静默。** 说明三个特性位不是根因（或不是唯一根因）。

### 2026-11-02 把观测点下沉到 PHY：到底有没有把 EPR_Mode 发出去

应用层日志能区分"PE 没构造报文"和"报文构造了但没上总线"吗？**不能**。所以在 ST 设备层加了三个只入队、不打印的探针（`Firmware/Middlewares/ST/STM32_USBPD_Library_v541/Devices/STM32G0XX/src/usbpd_phy_hw_if.c` 与 `usbpd_hw_if_it.c`）：

| 新日志 | 位置 | 含义 |
|---|---|---|
| `PD_TX a=<类型> b=<字节数>` | `USBPD_HW_IF_SendBuffer()` 入口 | 栈真的调用 PHY 发送了某类报文（每种类型只记一次）。**`a=10` 就是 EPR_Mode** |
| `PD_TXREJ a=<类型> b=<status>` | 同一函数的"RX 正在收 / DMA 忙 → 丢弃"分支 | PHY 拒绝发送（`USBPD_ERROR`）。PE 若不重试，看起来就跟"什么都没发生"一样 |
| `PD_TXDONE a=0/1/2` | UCPD 中断里的 TX 完成 | `0`=TXMSGSENT、`1`=TXMSGDISC（被丢弃）、`2`=TXMSGABT（被中止），各记一次 |

PD 报文类型编号（Header bits 4..0）：`1`=Source_Capabilities、`2`=Request、`4`=Sink_Capabilities、`10`=**EPR_Mode**、`15`=Vendor_Defined、`0`=Control（GoodCRC/Accept/PS_RDY 等）。

判读：

| 现象 | 结论 | 下一步 |
|---|---|---|
| 有 `PD_TX a=10`，随后源端无反应 | EPR_Mode 真的发出去了，源端没答（或答了但被 PE 丢弃） | 需要 CC 分析仪看源端有没有回 EPR_Mode（data=4 即拒绝） |
| 有 `PD_TX a=10` + `PD_TXDONE a=1/2` | UCPD 把我们的 EPR_Mode 丢了/中止了 | 我方 PHY/CC 时序问题，可改 |
| **没有** `PD_TX a=10` | PE 连发送都没尝试 | 只可能是 ST 库内部状态机问题，证据已足够提交 ST 支持 |
| `PD_TXREJ a=10` | PHY 因 RX 未让出总线而拒发 | 属我方集成问题 |

Rebuild：0 Error / 0 Warning，`Code=79,092 RO=4,096 RW=468 ZI=21,196`。

### 2026-11-02 反汇编 `USBPD_PE_Request_EPRModeEnter()`：它只"登记"，不发送

`fromelf --text -s/-c` 解出该函数（`0x0800f61c`，68 字节）的实际逻辑：

```asm
r2 = PE_Handle[PortNum]                 ; 0x200001a8 + 4*port
r3 = r2 + (0x13<<5)                     ; +0x260，即通用状态机上下文
if (byte[r3+0xA] != 0) return 3         ; BUSY：已有一个 EPR 机器挂起
r4 = word[r2+4]                         ; 当前/最近一条报文（含 2 字节头）
if ((byte[r4+1] & 0x10) == 0) return 3  ; 要求是带 1 个 DO 的报文
if ((byte[r4+1] & 0x07) != 3) return 2
if ((byte[r4+0] & 0x07) != 2) return 2  ; 要求报文类型 = Request(0x02)
byte[r3+0xA] = 0xA8                     ; 登记"要跑 EPR 进入机器"
word[r3+0]   = 1
call PE_Handle->callbacks[10]()          ; = USBPD_PE_TaskWakeUp()
return 0                                 ; USBPD_OK
```

而**真正发 EPR_Mode 的通用状态机只有一个调用点**：`USBPD_PE_StateMachine_SNK` 的 `PE_STATE_GENERIC` 分支里的 `PE_GENMACH_StateMachine_Run()`
（`0x08010370`，紧跟在 `MOVS r0,#0x13 / BL USBPD_PE_Notification` 之后）。

结论：`EPR_ENTER a=0` **不等于报文要发出去了**——它只是把 0xA8 写进 PE 的机器状态字节并唤醒 PE。如果 PE 不进入 `PE_STATE_GENERIC`，这个请求就永远挂着，于是：

- 第二次 `pd epr` 必然命中第一个判断 → `EPR_BUSY(3)`（与板端完全一致）；
- 没有 `GETINFO(30/25)`、没有任何 `EPR_EVT`、PHY 侧也没有 `PD_TX a=10`（与板端完全一致）。

所以这不是"数据没给对"，而是**库的状态机没被驱动**。按板端请求，不走 ST 支持，直接在本分支做两条旁路验证（`IH01A_EPR_STALL_FALLBACK`，仅在"已投递但 1 s 无任何 EPR 事件"时触发一次）：

| 实验 | 做法 | 预期日志 |
|---|---|---|
| **A** | `USBPD_PE_Request_DataMessage(0, USBPD_DATAMSG_EPR_MODE, &mdo)`，`mdo = (1<<24) \| (140<<16)`（EPR Mode Action=Enter、Sink PDP 140 W） | `EPR_ACT a=673 b=<rc>`（673=0x2A1），随后 `PD_TX a=10 b=…` |
| **B** | A 返回非 OK 时：用 `USBPD_PE_Send_Request()` 重发当前 SPR Request（制造 Request 上下文），并在 `POWER_EXPLICIT_CONTRACT` 通知里调用 `USBPD_PE_Request_EPRModeEnter()` | `EPR_ACT a=674 b=<rc>`（0x2A2）→ `EPR_ACT a=675 b=<rc>`（0x2A3）→ `PD_TX a=10` |

判读：**只要出现 `PD_TX a=10`，就说明 EPR_Mode 真的上总线了**；之后看源端是否回 `EPR_Mode`（源端 ACK 会带来 `EPR_EVT a=113`）。

Rebuild：0 Error / 0 Warning，`Code=79,556 RO=4,096 RW=468 ZI=21,196`。

**板端结果：两条旁路都返回 `USBPD_BUSY(3)`**（`EPR_ACT a=673 b=3`、`a=674 b=3`）。原因正是反汇编里那个 `byte[r3+0xA] = 0xA8`：第一次 `EPR_ENTER a=0` 已经把"EPR 机器挂起"写进 PE 句柄，而机器永远不跑，于是此后**任何**新请求（数据报文、重发 Request）都被判为 BUSY —— 库把自己锁死了。

### 2026-11-02 改为"只在 PE 回调上下文登记"（`IH01A_EPR_REENTER_ON_CONTRACT`）

API 的第一个判断就是"最近一条报文必须是 Request"，说明它的设计用法是：**刚发完 Request、PE 正在处理报文时**调用。所以把主循环里的那次调用去掉，改成：

1. `pd epr` → 用 `USBPD_PE_Send_Request()` 重发当前 SPR Request（同 20 V/5 A，制造 Request 上下文）→ `EPR_ACT a=674`（0x2A2）；
2. 源端 Accept → PS_RDY → `POWER_EXPLICIT_CONTRACT` 通知 → **在这个回调里**调用 `USBPD_PE_Request_EPRModeEnter()` → `EPR_ACT a=675`（0x2A3）；
3. 若 700 ms 仍无 EPR 事件：发一条 `Get_Source_Capability`（控制报文 0x07）主动制造一次 RX/报文周期 → `EPR_ACT a=676`（0x2A4）；
4. 若 1300 ms 仍无：直接 `USBPD_PE_Request_DataMessage(DATAMSG_EPR_MODE, (1<<24)|(140<<16))` → `EPR_ACT a=673`（0x2A1）。

判读：出现 `PD_TX a=10` 即代表 EPR_Mode 真的上了总线；出现 `EPR_EVT a=113/116/119` 代表源端已应答。

Rebuild：0 Error / 0 Warning，`Code=79,616 RO=4,096 RW=468 ZI=21,196`。

### 2026-11-02 28 V EPR 成功（首个成功会话）

```
EPR_REQUEST a=28000 b=0x81C7D1F4     ← OPOS=8（绝对位置）
PD_TXDO     do1=0x81C7D1F4 do2=0x0008C1F4
PD_RX  a=3 …                          ← 源端 Accept
CONTRACT a=1 …                        ← PS_RDY → 28 V/5 A 成立
之后每 ~485 ms：PD_TX a=16 do1=0x00038002（EPR KeepAlive，ExtType=3）+ 源端 ACK
```

**必须同时满足的 4 个条件（全部实证）**：

| # | 条件 | 违反时的现象 |
|---|---|---|
| 1 | `USBPD_HW_IF_IsResistor_SinkTxOk()` 必须为 TRUE（本板外部 Rd，UCPD 的 CC vstate 永不报 Rp-3.0 A）→ `IH01A_PD_FORCE_SINK_TXOK=1` | 所有应用主动发起的报文被静默丢弃：`EPR_ENTER a=0` 后无任何 `PD_TX`，随后全部 API 返回 BUSY |
| 2 | `USBPD_CORE_DATATYPE_REQ_COPYPDO` 只回 **4 字节**（被复制的源端 PDO） | EPR_Request 的 DO2 变成 RDO 副本（抓包可见 `2C B1 C4 11` 重复），源端 Hard Reset |
| 3 | RDO 置 `UnchunkedExtendedMessage`(bit23) + `EPR_Capable`(bit22) + `NoUSBSuspend`(bit24) | 缺 bit23 时源端把 EPR_Request 当协议违规，直接 Hard Reset（无 Reject/Not_Supported） |
| 4 | Object Position 用**绝对位置 8..13** | 用 1 基下标 1..6 同样被 Hard Reset（`pd epr1` 可复现） |

**其余必要设置**：`Is_EPR_Supported_SNK=1`、`PE_UnchunkSupport=1`、`Is_SrcCapaExt_Supported=1`；`SNK_PDP_EPR=140 W`；EPR 入口必须从 PE 回调上下文登记——先用 `USBPD_PE_Request_CtrlMessage(GET_SRC_CAP)` 制造一次 `Source_Capabilities → Request → PS_RDY` 周期，再在 `POWER_EXPLICIT_CONTRACT` 通知里调用 `USBPD_PE_Request_EPRModeEnter()`（从空闲主循环调用会让库把机器永久挂起）。

**诊断层**（这次能定位的关键）：`PD_TX`(类型+头)、`PD_TXDO`(前两个数据对象)、`PD_RX`(类型+头)、`TXDONE`(发送结果)、`EPR_EVT/EPR_ACT/GETINFO/SETINFO`。**周期性重复的帧（KeepAlive）只记录一次**，否则单个 EPR 会话会产生 ~242 B/s 的串口刷屏。

### 2026-11-02 默认自动申请 EPR（便于抓全流程）

- `IH01A_EPR_28V5A_AUTO_ENTER = 1`：SPR 显式合同建立 **250 ms** 后自动尝试 EPR；`PD_SourceIsEPRCapable()` 不成立（源端 PDO1 的 EPR Capable 位=0）时完全不尝试，直接留在 20 V。
- 失败即退回 SPR：`EPRMODE_FAILED/INVALID` → `PD_EPR_RestoreSPR()` + 退出 EPR；2.5 s 收不到 EPR 能力 → 发 `EPRModeExit` 退回；两种情况都**不会自动重试**（`s_epr_enter_once`，每次接入一次）。
- 自动模式下的一次冷启动序列（供逻辑分析仪对照）：`Source_Caps → Request(20V/5A) → Accept → PS_RDY → Get_Source_Cap → Source_Caps → Request → Accept → PS_RDY → EPR_Mode(Enter) → ACK → SUCCESS → EPR_Get_Source_Cap → EPR_Source_Caps → EPR_Request → Accept → PS_RDY → 28 V`，之后每 ~485 ms KeepAlive/ACK。
- CLI：`pd auto` 切换自动/手动（下次接入生效）；`pd epr` 手动触发（绝对位置）；`pd epr1` 用 1 基约定做对照（本机会被硬复位）。
- **已知副作用**：若源端谎报 EPR 能力且每次都硬复位，自动模式会形成"上电→申请 EPR→硬复位→上电"的循环；需要时先 `pd auto` 关掉再插入。

稳定运行记录：2026-11-02 连续 **6 小时**无断联。逻辑分析仪（PulseView 游标）实测 **KeepAlive 周期 500 ms**，在 PD3.1 的 250–500 ms 窗口内（取上限）。逻辑分析仪采样率 **4 MSa/s** 已确认（pd decode.txt 的时间戳是采样计数，除以 4e6 才是秒，之前把它当 µs 导致所有间隔被放大 3.86 倍）。此后 App 侧把 EPR KeepAlive 定速为 **375 ms**（仅在上一次 KeepAlive 距今 ≥250 ms 时补发，避免违反规范下限），栈自身的 500 ms 帧可能仍会与之交错，需抓包确认最终节奏。
