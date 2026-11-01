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
