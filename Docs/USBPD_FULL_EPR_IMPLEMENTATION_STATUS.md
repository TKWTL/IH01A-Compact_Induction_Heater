# IH01A FULL EPR 28V/5A 软件迁移状态

**分支**：`feature/pd-epr-28v5a`；基点 `main@668e541`。`main` 未被改动。

**重要**：本分支已经替换编译配置并准备了 EPR 请求代码，但当前执行环境没有 Keil Arm Compiler 6、板端或 140 W 输入硬件验证。**尚未证明能够编译、保持 20V SPR 或请求 28V**，禁止直接作为生产固件。

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
