> **当前状态请看 Docs/HANDOFF_EPR_NEXT.md**（2026-11-02：SPR 20V/5A + EPR 28V/5A 已板端打通）。
> 本文保留为迁移过程的历史记录。

# 交接：IH01A USB-PD FULL/EPR 迁移（下一棒从这里继续）

**分支**：`feature/pd-epr-28v5a`　**基点**：`main@668e541`　**HEAD 见 `git log -1`**（本文档随代码一起提交）
**当前状态**：FULL 栈（v5.4.1）已链接并**板端 20 V SPR 回归通过**（`SPR_CAPS a=6` → `REQUEST a=20000` → `CONTRACT`，实测 20.048 V）。EPR 许可已由硬件负责人改为 `IH01A_EPR_28V5A_HARDWARE_APPROVED=1`，但 `..._AUTO_ENTER=0`：**只有串口 `pd epr` 才会试 28 V**，普通插电仍停在 20 V。
**待做**：28 V 空载首测（PWM 禁开、独立 OVP、240 W e-mark 线）；细节与里程碑见 `Docs/USBPD_FULL_EPR_IMPLEMENTATION_STATUS.md`。

---

## 1. 这条分支已经完成的事

| 提交 | 内容 | 验证 |
|---|---|---|
| `55cddb9` | `pd_select.{c,h}` 增加 EPR 选档预备：`PD_INPUT_EPR_MAX_MA 5000`、`PD_Input_BestEPRFixed()`、`PD_Input_EPRFixedRDO()`、`PD_Input_FixedRDOForEPR()`；`test_pd_select.c` 增加 28V/5A、AVS 排除、36V 越界用例 | host 测试通过 |
| `dcf687b` | 修 `Tests/host/run.sh`：Windows 的 `snprintf` 需 `-D__USE_MINGW_ANSI_STDIO=1`；修 `\\` 续行、输出目录、`build_run` 封装 | 7/7 用例通过 |
| `814a81f` | vendored ST FULL 中间件 v5.4.1（未接入编译）+ 影响面清单 | 批处理编译 0 error / 0 warning |
| `753c4d1` | `.ioc` 取消勾选 USBPD（TIM3 释放）+ 恢复 PD plumbing（`usbpd.h` include、SysTick/IRQ 分发、8 KiB heap、FULL define/库/include） | Rebuild 通过 |
| `862beb7` | `USBPD_DPM_Run()` 搬进 `USER CODE BEGIN 3`（CubeMX 生成时删掉了它 → 调度器全线不跑，卡死在 `main.c` 的 `while(1)`） | Code 48,476 → 75,936 |
| `6f4b5e0` | `UCPD1_2_IRQHandler` 里恢复 `USBPD_PORT0_IRQHandler()`（同样被 CubeMX 删掉 → UCPD 标志永不清理 → 中断风暴，调试器停在 ISR 里） | `Code=76,316 RO=3,736 RW=464 ZI=21,024`，0 error / 0 warning |
| `9762dee` | `cubemx_post_generate.ps1` 的 `USBPD_PORT0_IRQHandler()` 检查修成独立 if/else（原先多出一个悬挂 `else`，脚本直接退 1） | 干净状态下运行 exit 0 |
| （`main` 上的 `0991678`/`187114b`/`668e541`） | uvprojx / uvoptx 的 Keil 原生序列化基线、`RunUserProg2=0`、uvoptx 重新纳入跟踪 | — |

编译实测（Keil 批处理，`6f4b5e0`）：

```
Code=76,316  RO-data=3,736  RW-data=464  ZI-data=21,024     0 Error(s), 0 Warning(s)
```

**注意**：新增的 `PD_Input_*EPR*` 函数**能编过但不在镜像里**——`One ELF Section per Function` + 链接器 gc 掉了（`fromelf` 已确认：`pd_select.o` 里有、`.axf` 里没有）。这是设计意图，不是漏编。

## 2. 关键事实（都实测过，别再猜）

| 事实 | 数据 |
|---|---|
| 当前链接的 PD 栈 | `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib`（**140,892 B**），宏 `USBPDCORE_LIB_PD3_FULL`；**MINSNK 旧库已不在工程里** |
| 当前 USBPD 头文件 | 全部来自 vendored `STM32_USBPD_Library_v541`（v5.4.1 `aafa359`），`EPR` API 可用 |
| 新版 EPR API | `USBPD_PE_Request_EPRModeEnter(uint8_t PortNum)` / `...EPRModeExit(...)`（`usbpd_core.h`） |
| 设备层强依赖 | 7 个 `.c` 全部 `#include "usbpd_devices_conf.h"`；已按 UCPD2/CH6-CH7/TIM3 派生 |
| 应用层 | `USBPD/App/usbpd_dpm_core.c` 已整体换成 FULL 版（30,189 B 口径，1019 行 + 诊断宏）；`usbpd_dpm_user.c` 的 EPR 代码在 `#if` 闸门后 |
| 内存占用（实测 `6f4b5e0`） | Flash 128 KiB：Code 76,316 + RO 3,736（约 60%）；RAM 36 KiB：RW 464 + ZI 21,024 + **栈 1 KiB + microLIB 堆 8 KiB** |
| 工具链配置 | **MicroLIB 开启**（`<useUlib>1`）、`mini_stdio.c` 已从 Target 排除、`retarget.c` 提供 `_sys_*` 桩（无半主机） |

## 3. 纪律与坑（踩过的，别再来一次）

1. **改任何编译选项后必须 Rebuild**。uVision 不会因为选项变化重编汇编文件——之前 4 个 `L6218E`（`__initial_sp`/`__heap_base`/`__heap_limit`/`__use_two_region_memory`）就是 `startup_stm32g071xx.o` 的旧对象导致的。
2. **MicroLIB 不需要手写 `__MICROLIB` 宏**：勾选 Use MicroLIB 时 Keil 自己会传给 C 编译器和汇编器；手工往 Define 里加反而多余。
3. **不要手写 RDO 位**。`pd_select.c` 里 `(1u<<22)` 当 "EPR capable"、`(1u<<24)` 当 "NoUSB suspend" 是**未经证实**的编码（按 PD 规范 RDO：B23=Capability Mismatch、B22=USB Comm Capable、B21=No USB Suspend、B20=Unchunked），"EPR Capable" 是 **Source Fixed PDO 的 B23**。迁移到 FULL 栈时必须用 ST 头文件的宏/结构体重写，否则一旦被调用就是错误请求。
4. **新旧中间件不能混用**：`.../STM32_USBPD_Library`（旧 MINSNK）与 `.../STM32_USBPD_Library_v541`（新 FULL）头文件结构体/枚举位置不同，一次只能用一套。
5. **`uvoptx` 已重新纳入版本控制**（承载 CMSIS-DAP 调试器选择与 `STM32G0xx_128.FLM` Flash 算法）。Keil 打开/关闭后若它变脏，直接提交即可；`uvprojx`/`uvoptx` 都已有 Keil 原生序列化基线，不会再有整文件格式 diff。
6. **host 测试在 Windows 上**：`CC=<mingw gcc> sh Firmware/Tests/host/run.sh`，脚本已带 `-D__USE_MINGW_ANSI_STDIO=1`（否则 `test_time` 的 `size=1 应输出空串` 断言会假失败——那是 MSVCRT 的 `_snprintf` 语义，不是固件 bug）。
7. `Docs/USBPD_EPR_MIGRATION.md` 是 2026-11-01 的旧记录，其中"电流钳到 3 A"已被本分支的 5 A EPR 上限取代，以本文件和 `Docs/USBPD_FULL_EPR_MIGRATION_IMPACT.md` 为准。
8. **`memcpy` 声明被条件编译挡掉**：`usbpd_dpm_user.c` 里 ST 把 `#include "string.h"` 放在 `#if defined(_TRACE)` 内，本工程不定义 `_TRACE`，于是新加的 EPR 代码用 `memcpy` 会报 `-Wimplicit-function-declaration`。已在 `USER CODE BEGIN Includes` 里显式加 `<string.h>`。
9. **microLIB 堆必须放大到 8 KiB**：FULL 栈用 `malloc` 建 PE/PRL/CAD 句柄，实测需求 `PRL 0x396 + CAD 0x24 + PE (0x6F<<5) = 4506 B`（双口口径；`USBPD_PE_GetMemoryConsumption()` 返回值）。startup 原来 `Heap_Size = 0x400` 只有 1 KiB，`USBPD_PE_Init()` 直接返回 `USBPD_MALLOCERROR`（枚举值 21 / 0x15），表现为 `MX_USBPD_Init()` 里 `while(1)` 死等。现为 `0x2000`（RAM 静态占用约 22.5/36 KiB）。
10. **CubeMX 可以继续用来改配置，但必须走"生成 → 跑脚本 → Rebuild"三步**（见 §4.2）。直接 Generate 会：覆盖 `Firmware/USBPD/**` 与 `_v541`、删掉 `stm32g0xx_ll_tim.c`、把 `uvprojx` 的 define/库/include 改回 MINSNK、删掉 IH01A 自己的文件组，并删除非 USER-CODE 区域的关键调用——已实测被删过的有 `DebugUART_Init()`（串口全哑）、`USBPD_DPM_Run()`（调度器不跑，停在 `main.c` 的 `while(1)`）、`USBPD_DPM_TimerCounter()`（PD 无 1 ms 时基）、`USBPD_PORT0_IRQHandler()`（UCPD 标志不清理 → 中断风暴，调试器一直停在 `UCPD1_2_IRQHandler`）。这四处现在都住在 USER CODE 里，脚本的 3b 段会逐个校验。

## 4.2 CubeMX 工作流（已加护栏，2026-11-02）

设计目标：以后仍然可以用 CubeMX 改引脚/时钟/heap，但不能再把 PD 栈与串口搞坏。

| 位置 | 做法 |
|---|---|
| `main.c` | 与 CubeMX 无关的调用全部搬进 `USER CODE`：`DebugUART_Init()`、`MX_USBPD_Init()`（BEGIN 2）、`USBPD_DPM_Run()`（BEGIN 3）、`g_uart_boot_stage` 全部在手写区，重新生成不会丢 |
| `main.c` `USER CODE BEGIN 0` | 加了**生成护栏**：若 `USBPDCORE_LIB_PD3_FULL` 未定义（= 刚被 CubeMX 重写过），直接 `#error` 并提示去跑脚本 —— 不会静默编出 MINSNK 版本 |
| `stm32g0xx_it.c` | `USBPD_DPM_TimerCounter()`（SysTick 1 ms）与 `USBPD_PORT0_IRQHandler()`（`UCPD1_2_IRQHandler`）都在 USER CODE 里 |
| `Firmware/Tools/cubemx_post_generate.ps1` | 一键修复：恢复 `uvprojx` / `Firmware/USBPD` / `_v541` / `stm32g0xx_ll_tim.c` 为仓库版本 → 把新生成/已删除的 `Core/Src/*.c` 同步进工程 → 校验 define/库/include/旧目录残留/heap → 校验 4 个 USER CODE 关键调用。幂等，干净状态下运行不改一个字节 |
| `.ioc` | `ProjectManager.HeapSize=0x2000` 已写入；USBPD 中间件**已取消勾选**（TIM3 不再被 CubeMX 占用，PD 时基由 `USBPD_TIM_Init()` 自己配） |

标准流程：

```powershell
# 1) CubeMX 里改配置 → Generate Code
# 2) 修复工程并校验
powershell -ExecutionPolicy Bypass -File Firmware/Tools/cubemx_post_generate.ps1
# 3) Keil: Rebuild（若跳过第 2 步，main.c 的 #error 会挡住编译并提示跑脚本）
```

USBPD 中间件已在 `.ioc` 里**取消勾选**（`753c4d1`）：CubeMX 不再生成 `Firmware/USBPD/**`，CubeMX 也不再占用 TIM3（PD 时基由 `USBPD_TIM_Init()` 自己配）。若将来想改回去，勾上之后每次 Generate 都会把那 5 个文件写成旧模板，`git diff` 里会出现噪音。

## 4. 下一棒：按这个顺序做，每步都有验收标准

### 步骤 1　生成 `usbpd_devices_conf.h`　✅ 已完成
- 从 `Firmware/Middlewares/ST/STM32_USBPD_Library_v541/Devices/STM32G0XX/inc/usbpd_devices_conf_template.h` 派生，只保留 UCPD2 相关项：`UCPD_INSTANCE0=UCPD2`（唯一逻辑端口 0）、RX(`DMA1`/`DMA_REQUEST_UCPD2_RX`/`LL_DMA_CHANNEL_6`/`DMA1_Channel6`)、TX(`DMA1`/`DMA_REQUEST_UCPD2_TX`/`LL_DMA_CHANNEL_7`/`DMA1_Channel7`)、`UCPD_INSTANCE1_ENABLEIRQ`；FRS 段按未使用处理。
- 使用已有的 `Firmware/USBPD/Target/usbpd_devices_conf.h`，适配 FULL 单端口 UCPD2 后保留 TIM3 时基。**错误的 2/4 通道会分别冲突 ADC DMA CH2 和 UART RX DMA CH4。**
- **验收**：与 `Firmware/Core/Src/dma.c` 里 UCPD2 的实际 DMA 通道、`usbpd.c` 里的 UCPD2 初始化逐项对上；此步不接入编译。

### 步骤 2　换 `usbpd_dpm_core.c`　✅ 已完成
- 用新版 `stm32-mw-usbpd-core/src/usbpd_dpm_core.c`（30,189 B）替换工程的 14,028 B 版本，按本板裁剪（单端口、sink、无 VDM）。
- **验收**：Keil Rebuild 0 error；`.map` 里 `Code/RO` 增量记录在案；`USBPD_PE_CheckLIB()` 返回 OK。

### 步骤 3　切换 `.uvprojx`　✅ 已完成（`753c4d1`，FULL define + 140,892 B 库 + v541 include）
按 `Docs/USBPD_FULL_EPR_MIGRATION_IMPACT.md` §3 的 7 项：define 换 `USBPDCORE_LIB_PD3_FULL`、库路径、两条 IncludePath、`usbpd_trace.c`、7 个设备层 `.c` 路径。
**验收**：Rebuild 通过，且 20 V 协商**仍然成功**。

### 步骤 4　20 V 回归　✅ 通过（2026-11-02，板端实测 20.048 V）
- 保持 `PD_Input_BestFixed()` 的 SPR 路径，请求 20V/3A。
- **实测**：`[PD_INIT] LIB_ID=0x30410000 check=1 reqmem=4506 heap=8192` → `FULL v5.4.1 initialized` → `SPR_CAPS a=6` → `REQUEST a=20000` → `CONTRACT a=1`；`[CLI] bus=20048 mV select=20000 mV current=3000 mA PDO=5`。UART/ADC/OLED 正常。
- 一次「已带电时 MCU 单独复位」出现多轮 HARD_RESET（Source 仍持旧契约、未重发 Capabilities），断电重上电即恢复；不阻塞 EPR 首测，但要留意冷启动时序。

### 步骤 5　EPR 试入　✅ 2026-11-02 空载达成 28 V/5 A（KeepAlive ~485 ms 正常）
- 许可：`IH01A_EPR_28V5A_HARDWARE_APPROVED=1`（硬件负责人确认输入通路可承受 28 V）；自动进入仍关闭（`..._AUTO_ENTER=0`），入口是串口 `pd epr`。
- 已接：EPR Mode Enter/Exit、独立 EPR PDO 数组解析（1..6 对象 → 位置 8..13，兼容合并布局）、28 V/5 A 选档、8 字节 RDO+CopyPDO、2.5 s 能力超时、拒绝/失败回退 SPR。
- 诊断码（2026-11-02 新增，用于定位"EPR 进不去"）：`EPR_ENTER` 记原始 `USBPD_StatusTypeDef`；`EPR_EVT` 记全部 112..119 通知并打印事件名（`a=0 b=0xD00D` = 请求投递后 1 s 内 PE 完全静默）；`EPR_ACT` 记 PE 的 `USBPD_ACTION_*` 提问与我们的回答（4=CHECK_PDO、481=SNK_PDP_EPR、482=SNK_PDO_EPR）；`GETINFO`/`SETINFO` 记 PE 每次取/送数据的 `DataId` 与字节数（每个 id 只记第一次）。判读表在 `Docs/USBPD_FULL_EPR_IMPLEMENTATION_STATUS.md`。
- **验收**：见 `Docs/USBPD_FULL_EPR_IMPLEMENTATION_STATUS.md` 的里程碑与异常定位表；`EPR_CAPS a` 的判读也写在那份文档里。
- 设置 sink EPR 支持位与 Operational PDP（28V×5A=140W）；SPR 契约稳定后调用 `USBPD_PE_Request_EPRModeEnter(0)`。
- 处理成功/失败通知，读 EPR Source Capabilities，选 28V/5A（拒绝 36/48V），发 **EPR Request（RDO + 匹配的 CopyPDO）**。
- **失败一律回落 SPR ≤20V，不自动重试**（只允许 UI 手动 Retry）。
- **验收**：串口里程碑 `EPR ENTER -> EPRMODE_SUCCEEDED -> EPR CAPS -> EPR REQUEST -> PS_RDY`，且 EPR 会话期间 PD/UI/UART 都活着。

## 4.1 CubeMX 与本项目 USBPD 的关系（2026-11-02 实测）

CubeMX 的 **Software Packs / Additional Software** 列表里没有单列的 USBPD 包——它是随 **STM32Cube FW G0** 固件包以 **Middleware `STM32_USBPD_Library`** 形式提供的。
本机 CubeMX 6.17.0 的仓库 `D:\Program Files\STMicroelectronics\STM32Cube\Repository\STM32Cube_FW_G0_V1.6.3\Middlewares\ST\STM32_USBPD_Library` 实测：

| 项 | CubeMX 自带 (FW G0 v1.6.3) | 本仓库 vendored (core v5.4.1) |
|---|---|---|
| `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib` | **有**，123,306 B | **有**，140,892 B |
| `usbpd_core.h` | 47,095 B，**`EPR` 出现 0 次** | 49,794 B，含 `USBPD_PE_Request_EPRModeEnter/Exit` |
| `usbpd_def.h` | 166,883 B | 188,382 B |
| `.ioc` 里的变体 | `VP_USBPD_VS_PD3MINSNK.Mode=PD3_Min_Snk` | — |

**结论：CubeMX 6.17 能配出 PD3.0 的 FULL，但配不出 EPR**——EPR 需要 core ≥ v5.x，而 G0 v1.6.3 固件包里的中间件是旧的。所以 EPR 版只能走仓库内 vendored 的 `STM32_USBPD_Library_v541`，CubeMX 不会管理它（也不会有"软件包列表里选 EPR"这条路）。

**由此带来的工程约束**（务必注意）：

- `ProjectManager.LibraryCopy=1`，CubeMX 会把旧版中间件复制到 `Firmware/Middlewares/ST/STM32_USBPD_Library`。**一旦在 CubeMX 里点 Generate Code，该目录会回到 v1.6.3 旧版内容**，并可能改写 `.uvprojx` 里的库/include 条目。
- vendored 目录用的是**版本化路径** `..._v541`，CubeMX 不认识，所以不会被它的生成覆盖——这是当初选这个目录名的原因。
- 建议在步骤 3 完成、20 V 回归通过后，把 USBPD 从 CubeMX 管理里摘出来（`.ioc` 去掉 `Mcu.IP18=USBPD` / `VP_USBPD_*`，或把 `ProjectManager.LibraryCopy` 置 0），之后 USBPD 目录完全由 git 维护；生成出来的 `usbpd.c`/`usbpd_pwr_if.c`/`usbpd_dpm_core.c` 已在仓库里，不影响。
- 若将来 CubeMX 或 X-CUBE-TCPP 提供 core ≥ v5 的中间件，应切回官方管理，减少手工同步成本。


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
