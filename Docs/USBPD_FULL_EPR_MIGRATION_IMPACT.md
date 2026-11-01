# IH01A：从 MINSNK 迁移到 FULL（PD3.1 EPR）影响面清单

> 状态：**仅调研与物料准备，未改动任何构建配置**。当前 Keil 工程仍链接旧的 MINSNK 库，
> 行为与 main 一致（20V/3A，实测正常）。
> 物料已入库：`Firmware/Middlewares/ST/STM32_USBPD_Library_v541/`（未加入编译）。

## 1. 现状（实测，非推测）

| 项 | 值 |
|---|---|
| 链接的库 | `Middlewares/ST/STM32_USBPD_Library/Core/lib/USBPDCORE_PD3_CONFIG_MINSNK_CM0PLUS_Keil.lib`（81,116 B） |
| 配置宏 | `USBPDCORE_LIB_PD3_CONFIG_MINSNK`（在 `<Define>` 里，与 `USE_FULL_LL_DRIVER,STM32G071xx,USBPD_PORT_COUNT=1,_SNK,USE_HAL_DRIVER` 同列） |
| Core 头文件 | 3 个：`usbpd_core.h`(47,095 B)、`usbpd_def.h`(166,883 B)、`usbpd_trace.h`；**没有任何 EPR 符号**（`EPR` 出现 0 次） |
| 设备层 | `Devices/STM32G0XX`：7 个 `.c` + 6 个 `.h`（无 `usbpd_devices_conf.h`） |
| 工程侧源码 | `USBPD/App/{usbpd.c, usbpd_pwr_if.c, usbpd_dpm_core.c}`、`USBPD/Target/{usbpd_dpm_user.c, usbpd_pwr_user.c}` |
| 当前镜像 | `Code=59,192  RO=3,060  RW=464  ZI=13,840`（Flash 128 KiB / RAM 36 KiB，余量约 69 KiB / 22 KiB） |

## 2. 新物料（已 vendored，来源可追溯）

| 组件 | 版本 | 文件数 | 大小 |
|---|---|---|---|
| Core（inc + FULL lib + trace） | `aafa359` Release v5.4.1 | 13 | 397 KB |
| Device layer STM32G0XX | `527363f` | 15 | 195 KB |

关键新增：

- `Core/inc/usbpd_pe_epr.h`、`usbpd_pe_extctrl.h`、`usbpd_pe_genericmachine.h`、`usbpd_tcpm.h`
- `Core/inc/usbpd_core.h` 新增 **`USBPD_PE_Request_EPRModeEnter(uint8_t PortNum)`** / `...EPRModeExit(...)`
- `Core/lib/USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib`（**140,892 B**）
- `Devices/STM32G0XX/inc/usbpd_devices_conf_template.h`（7,873 B）——**新版设备层强制要求的板级配置头**

版本台账见 `Firmware/Middlewares/ST/STM32_USBPD_Library_v541/VERSION.md`。

## 3. `.uvprojx` 需要的改动（切换时才做，做成**一个**可 revert 的提交）

| # | 位置 | 现在 | 改为 |
|---|---|---|---|
| 1 | `<Define>` | `USBPDCORE_LIB_PD3_CONFIG_MINSNK` | `USBPDCORE_LIB_PD3_FULL` |
| 2 | 库文件 `<FilePath>` | `.../STM32_USBPD_Library/Core/lib/USBPDCORE_PD3_CONFIG_MINSNK_CM0PLUS_Keil.lib` | `.../STM32_USBPD_Library_v541/Core/lib/USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib` |
| 3 | `<IncludePath>` | `../Middlewares/ST/STM32_USBPD_Library/Core/inc` | `../Middlewares/ST/STM32_USBPD_Library_v541/Core/inc` |
| 4 | `<IncludePath>` | `../Middlewares/ST/STM32_USBPD_Library/Devices/STM32G0XX/inc` | `../Middlewares/ST/STM32_USBPD_Library_v541/Devices/STM32G0XX/inc` |
| 5 | `<IncludePath>` 新增 | — | `../USBPD/Target`（放新生成的 `usbpd_devices_conf.h`；该目录本来就在 include 列表里，确认即可） |
| 6 | `usbpd_trace.c` | `.../STM32_USBPD_Library/Core/src/usbpd_trace.c` | `..._v541/Core/src/usbpd_trace.c` |
| 7 | 设备层 7 个 `.c` | `.../STM32_USBPD_Library/Devices/STM32G0XX/src/*.c` | `..._v541/Devices/STM32G0XX/src/*.c` |

旧的 `STM32_USBPD_Library/` 目录**保持不动**直到新版验证通过，作为回滚路径；回滚只需 revert 上述一个提交。

## 4. 必须新增的工程侧文件：`usbpd_devices_conf.h`

新版设备层 7 个 `.c` 全部 `#include "usbpd_devices_conf.h"`（旧版没有这个依赖）。最小内容由模板 `usbpd_devices_conf_template.h` 派生，本板只需保留 UCPD2 相关项：

- `UCPD_INSTANCE1  UCPD2`
- RX：`DMA1` / `DMA_REQUEST_UCPD2_RX` / `LL_DMA_CHANNEL_2` / `DMA1_Channel2`
- TX：`DMA1` / `DMA_REQUEST_UCPD2_TX` / `LL_DMA_CHANNEL_4` / `DMA1_Channel4`
- `UCPD_INSTANCE1_ENABLEIRQ`（UCPD2 中断使能宏）
- FRS 段：本板未用 FRS，按“不启用”配置

需与 `Core/Src/dma.c` 的实际 DMA 通道分配核对后再落笔（当前 `MX_DMA_Init` 里 UCPD2 RX/TX 用的是哪些通道，要一一对上）。

## 5. 应用层需要跟进的地方

| 文件 | 事项 |
|---|---|
| `USBPD/App/usbpd_dpm_core.c` | 本地 14,028 B，新版 30,189 B。**`DPM_Settings` / `DPM_Params` / `PE_PD3_Support` 等结构成员在新版里有变化**（自动扫描：应用层引用的 382 个 `USBPD_/DPM_/PE_` 符号中，有 180 个在新版 Core 头文件中找不到），此文件需要整体换成新版再按本板裁剪 |
| `USBPD/Target/usbpd_dpm_user.c` | 增加 EPR 回调：EPR 模式进入结果、EPR Source Capabilities 解析、EPR Request 发送（RDO + CopyPDO）、KeepAlive/退出；设置 sink EPR 支持位 |
| `USBPD/Target/usbpd_pwr_user.c` | 上报 sink 的 EPR 能力与 Operational PDP（28V/5A = 140W），并把 OVP/UVP 阈值按 28V 语义重新界定 |
| `Firmware/APP/algorithm/pd_select.c` | 现有 `PD_Input_*EPR*` 助手是用**手写 RDO 位**实现的（`1u<<22` 当 EPR capable、`1u<<24` 当 NoUSB suspend）。迁移时必须改成 FULL 栈头文件里的宏/结构体，否则一旦被调用就是错误请求 —— 这也是为什么它现在不参与链接 |

## 6. 风险与验证顺序

1. **内存**：FULL lib 140,892 B（MINSNK 81,116 B），但**归档大小 ≠ 链接后占用**。切换后先看 `.map` 的 `Code/RO-data/RW-data/ZI-data`，确认 Flash 余量（当前约 69 KiB）与 RAM 余量（约 22 KiB）足够。
2. **先做 20V 回归**：保留 `PD_Input_BestFixed()` 的 SPR 路径不变，`USBPDCORE_LIB_PD3_FULL` 下仍只请求 20V/3A，确认 `USBPD_PE_CheckLIB()` 通过、PD 状态机与 1 ms 定时器正常、ADC 采样不退化。
3. **再做 EPR 试入**：仅在 SPR 契约稳定后调用 `USBPD_PE_Request_EPRModeEnter(0)`，失败一律回落 SPR ≤20V，不自动重试（只允许 UI 手动 Retry）。
4. **硬件闸门**：28V/5A 前必须确认 Type-C 与 XT30 共用输入路径的器件/Caps 耐压、过流与 OVP 独立于软件；首次上电空载、PWM 关闭、限流源供电（详见 `Docs/USBPD_EPR_28V_PLAN.md` 第 28–32 行）。

## 7. 下一步（建议顺序）

1. 按 §4 生成 `usbpd_devices_conf.h`（先只对齐 UCPD2 的 DMA/IRQ 宏）；
2. 换 `usbpd_dpm_core.c` 为新版并按本板裁剪，补齐 180 处符号差异；
3. 按 §3 改 `.uvprojx`，Keil **Rebuild**，看 `.map` 与 `USBPD_PE_CheckLIB()`；
4. 20V 回归 → 再碰 EPR。
