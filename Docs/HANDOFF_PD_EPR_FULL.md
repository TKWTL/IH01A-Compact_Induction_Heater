# 交接：IH01A USB-PD FULL/EPR 迁移（下一棒从这里继续）

**分支**：`feature/pd-epr-28v5a`　**HEAD**：`814a81f`　**基点**：`main@668e541`
**当前可运行状态**：板端 20 V 协商正常（实测 ~20.08 V），加热 PWM 关闭；镜像行为与 main **完全一致**。

---

## 1. 这条分支已经完成的事（4 个提交）

| 提交 | 内容 | 验证 |
|---|---|---|
| `55cddb9` | `pd_select.{c,h}` 增加 EPR 选档预备：`PD_INPUT_EPR_MAX_MA 5000`、`PD_Input_BestEPRFixed()`、`PD_Input_EPRFixedRDO()`、`PD_Input_FixedRDOForEPR()`；`test_pd_select.c` 增加 28V/5A、AVS 排除、36V 越界用例 | host 测试通过 |
| `dcf687b` | 修 `Tests/host/run.sh`：Windows 的 `snprintf` 需 `-D__USE_MINGW_ANSI_STDIO=1`；修 `\\` 续行、输出目录、`build_run` 封装 | 7/7 用例通过 |
| `814a81f` | vendored ST FULL 中间件 v5.4.1（未接入编译）+ 影响面清单 | 批处理编译 0 error / 0 warning |
| （`main` 上的 `0991678`/`187114b`/`668e541`） | uvprojx / uvoptx 的 Keil 原生序列化基线、`RunUserProg2=0`、uvoptx 重新纳入跟踪 | — |

编译实测（Keil 批处理）：

```
Code=59,192  RO-data=3,060  RW-data=464  ZI-data=13,840     0 Error(s), 0 Warning(s)
```

**注意**：新增的 `PD_Input_*EPR*` 函数**能编过但不在镜像里**——`One ELF Section per Function` + 链接器 gc 掉了（`fromelf` 已确认：`pd_select.o` 里有、`.axf` 里没有）。这是设计意图，不是漏编。

## 2. 关键事实（都实测过，别再猜）

| 事实 | 数据 |
|---|---|
| 当前链接的 PD 栈 | `USBPDCORE_PD3_CONFIG_MINSNK_CM0PLUS_Keil.lib`（81,116 B），宏 `USBPDCORE_LIB_PD3_CONFIG_MINSNK` |
| 现有 USBPD 头文件 | `usbpd_core.h`/`usbpd_def.h`/`usbpd_trace.h`，**`EPR` 关键字出现 0 次** |
| 新版 FULL 库 | `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib`（**140,892 B**），v5.4.1 `aafa359` |
| 新版 EPR API | `USBPD_PE_Request_EPRModeEnter(uint8_t PortNum)` / `...EPRModeExit(...)`（`usbpd_core.h`） |
| 新版设备层强依赖 | 7 个 `.c` 全部 `#include "usbpd_devices_conf.h"`（旧版没有此依赖，仓库里也没有这个文件） |
| 应用层 API 落差 | 应用层引用 382 个 `USBPD_/DPM_/PE_` 符号，新版 Core 头文件里**缺 180 个**（`DPM_Settings`/`DPM_Params`/`PE_PD3_Support` 等）→ `USBPD/App/usbpd_dpm_core.c` 需整体换新（本地 14,028 B vs 新版 30,189 B） |
| 内存余量 | Flash 128 KiB（已用 ~59 KiB + 3 KiB RO），RAM 36 KiB（ZI 13.8 KiB + 栈/堆各 1 KiB） |
| 工具链配置 | **MicroLIB 开启**（`<useUlib>1`）、`mini_stdio.c` 已从 Target 排除、`retarget.c` 提供 `_sys_*` 桩（无半主机） |

## 3. 纪律与坑（踩过的，别再来一次）

1. **改任何编译选项后必须 Rebuild**。uVision 不会因为选项变化重编汇编文件——之前 4 个 `L6218E`（`__initial_sp`/`__heap_base`/`__heap_limit`/`__use_two_region_memory`）就是 `startup_stm32g071xx.o` 的旧对象导致的。
2. **MicroLIB 不需要手写 `__MICROLIB` 宏**：勾选 Use MicroLIB 时 Keil 自己会传给 C 编译器和汇编器；手工往 Define 里加反而多余。
3. **不要手写 RDO 位**。`pd_select.c` 里 `(1u<<22)` 当 "EPR capable"、`(1u<<24)` 当 "NoUSB suspend" 是**未经证实**的编码（按 PD 规范 RDO：B23=Capability Mismatch、B22=USB Comm Capable、B21=No USB Suspend、B20=Unchunked），"EPR Capable" 是 **Source Fixed PDO 的 B23**。迁移到 FULL 栈时必须用 ST 头文件的宏/结构体重写，否则一旦被调用就是错误请求。
4. **新旧中间件不能混用**：`.../STM32_USBPD_Library`（旧 MINSNK）与 `.../STM32_USBPD_Library_v541`（新 FULL）头文件结构体/枚举位置不同，一次只能用一套。
5. **`uvoptx` 已重新纳入版本控制**（承载 CMSIS-DAP 调试器选择与 `STM32G0xx_128.FLM` Flash 算法）。Keil 打开/关闭后若它变脏，直接提交即可；`uvprojx`/`uvoptx` 都已有 Keil 原生序列化基线，不会再有整文件格式 diff。
6. **host 测试在 Windows 上**：`CC=<mingw gcc> sh Firmware/Tests/host/run.sh`，脚本已带 `-D__USE_MINGW_ANSI_STDIO=1`（否则 `test_time` 的 `size=1 应输出空串` 断言会假失败——那是 MSVCRT 的 `_snprintf` 语义，不是固件 bug）。
7. `Docs/USBPD_EPR_MIGRATION.md` 是 2026-11-01 的旧记录，其中"电流钳到 3 A"已被本分支的 5 A EPR 上限取代，以本文件和 `Docs/USBPD_FULL_EPR_MIGRATION_IMPACT.md` 为准。

## 4. 下一棒：按这个顺序做，每步都有验收标准

### 步骤 1　生成 `usbpd_devices_conf.h`
- 从 `Firmware/Middlewares/ST/STM32_USBPD_Library_v541/Devices/STM32G0XX/inc/usbpd_devices_conf_template.h` 派生，只保留 UCPD2 相关项：`UCPD_INSTANCE1=UCPD2`、RX(`DMA1`/`DMA_REQUEST_UCPD2_RX`/`LL_DMA_CHANNEL_2`/`DMA1_Channel2`)、TX(`DMA1`/`DMA_REQUEST_UCPD2_TX`/`LL_DMA_CHANNEL_4`/`DMA1_Channel4`)、`UCPD_INSTANCE1_ENABLEIRQ`；FRS 段按未使用处理。
- 放到 `Firmware/USBPD/Target/usbpd_devices_conf.h`。
- **验收**：与 `Firmware/Core/Src/dma.c` 里 UCPD2 的实际 DMA 通道、`usbpd.c` 里的 UCPD2 初始化逐项对上；此步不接入编译。

### 步骤 2　换 `usbpd_dpm_core.c`
- 用新版 `stm32-mw-usbpd-core/src/usbpd_dpm_core.c`（30,189 B）替换工程的 14,028 B 版本，按本板裁剪（单端口、sink、无 VDM）。
- **验收**：Keil Rebuild 0 error；`.map` 里 `Code/RO` 增量记录在案；`USBPD_PE_CheckLIB()` 返回 OK。

### 步骤 3　切换 `.uvprojx`（做成一个可 revert 的提交）
按 `Docs/USBPD_FULL_EPR_MIGRATION_IMPACT.md` §3 的 7 项：define 换 `USBPDCORE_LIB_PD3_FULL`、库路径、两条 IncludePath、`usbpd_trace.c`、7 个设备层 `.c` 路径。
**验收**：Rebuild 通过，且 20 V 协商**仍然成功**。

### 步骤 4　20 V 回归（这一步不做完不许碰 EPR）
- 保持 `PD_Input_BestFixed()` 的 SPR 路径，请求仍 20V/3A。
- **验收**：`SPR_CAPS` → `SPR REQUEST` → `PS_RDY` 全链路正常，实测 VBUS ≈ 20 V，`USBPD_DPM_UserExecute` 周期无退化，ADC/OLED 不卡。

### 步骤 5　EPR 试入
- 设置 sink EPR 支持位与 Operational PDP（28V×5A=140W）；SPR 契约稳定后调用 `USBPD_PE_Request_EPRModeEnter(0)`。
- 处理成功/失败通知，读 EPR Source Capabilities，选 28V/5A（拒绝 36/48V），发 **EPR Request（RDO + 匹配的 CopyPDO）**。
- **失败一律回落 SPR ≤20V，不自动重试**（只允许 UI 手动 Retry）。
- **验收**：串口里程碑 `EPR ENTER -> EPRMODE_SUCCEEDED -> EPR CAPS -> EPR REQUEST -> PS_RDY`，且 EPR 会话期间 PD/UI/UART 都活着。

## 5. 硬件闸门（软件做完也不能直接上 28 V）

- 首次 28 V 必须**空载 + PWM 关闭 + 限流源**，用万用表/示波器确认 VBUS；
- Type-C 与 XT30 共用输入路径的器件、Caps、连接器、保险/过流都要按 28 V/5 A 及瞬态重新确认；
- 需要 240 W EPR 认证的 5 A 电子标签线缆；适配器必须真的提供 28V/5A EPR PDO；
- OVP 必须独立于 PD 软件。

## 6. 参考文档索引

| 文档 | 用途 |
|---|---|
| `Docs/USBPD_FULL_EPR_MIGRATION_IMPACT.md` | 影响面清单（现状实测、uvprojx 改法、应用层落差） |
| `Firmware/Middlewares/ST/STM32_USBPD_Library_v541/VERSION.md` | vendored 物料的仓库/提交/日期 |
| `Docs/USBPD_EPR_28V_PLAN.md` | 28V/5A 选档与硬件检查要求（部分内容为旧版） |
| `Docs/PD_UART_DIAG.md` | 串口诊断里程碑与状态机说明 |
| `Docs/NEXT_STAGE_PLAN.md` | 更宏观的 P0–P3 阶段计划（加热控制、测量、UI） |
