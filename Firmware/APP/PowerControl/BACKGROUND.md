# PowerControl — 移植背景说明

> 本文件由本仓库维护，不是源项目的原文；源项目自带的 `README.md` 已随目录一起复制进来。
> ⚠️ 源项目标注「学校内部资料，未经授权禁止外传」，本副本仅作本工程移植参考，请勿再分发。

## 1. 代码来源

| 项 | 内容 |
|---|---|
| 源仓库 | `NUEDC_PowerElectronics`（本地路径 `E:\Embedded_Full_ENG_Path\NUEDC_PowerElectronics`） |
| 源工程 | `Legacy_V2\NUEDC_2026A\Firmware_0` |
| 题目 | 2026 年全国大学生电子设计竞赛（TI 杯）本科组 **A 题：AC-AC 变换电路** |
| 主控 | **STM32G474VCT6**（Cortex-M4F @120 MHz，5 个 HRTIM 半桥，4×ADC 硬件同步采样） |
| 工具链 | Keil MDK + STM32CubeMX，带 FreeRTOS |
| 源工程完整文档 | `NUEDC_2026A\README.md`（688 行，含题目指标、拓扑选型、控制架构、测试项） |

**系统任务**：单相 36 V / 50 Hz 输入 → 全桥 PFC（HRTIM A/B，单极性倍频，PF ≥ 0.98）→ 直流母线（自适应）
→ 三相逆变（HRTIM C/D/E，30/60 Hz，THD ≤ 2%）→ 三相 32 V / 30~60 Hz 输出，效率 ≥ 95 %。

**控制策略要点**：母线电压外环 PI（多重陷波器抑制 50~250 Hz）→ 参考电流合成（归一化 + 数组延迟移相实现 PF 0.80~1.00 可调）
→ 准 PR 电流内环（无 PLL）→ DPWM-A / SVPWM 自适应调制 → HRTIM 占空比；自适应母线电压与死区；三级保护
（硬件比较器 + HRTIM FLT 逐周期封锁、ADC 看门狗 AWD、软件过流/过压/欠压/短路检测，支持自动恢复）。

## 2. 本目录是什么

`Firmware/APP/PowerControl/` 是源工程的**数字电源控制核心**，信号链
`ADC → Controller → Loop → PWM → 硬件`，不含 UI / 电源管理 / 外设初始化等其它层。

| 文件 | 角色 | 内容概要 |
|---|---|---|
| `conv_adc.c/h` | 采样 | HRTIM 触发同步采样、滑动滤波、9 路信号换算与 slope/zero 标定 |
| `conv_controller.c/h` | 状态机 | `CONV_STOP/RUN/PEND/FAULT` 状态切换、MainLoop 调度（开关频率级中断） |
| `conv_loop.c/h` | 环路 | 双 PR 电流内环（A/C 相 120°）、参考电流合成、PF 移相、DPWM-A 零序注入 |
| `conv_pwm.c/h` | 调制 | HRTIM 四单元占空比写入、ADC 触发时机延迟、DAC/死区配置 |
| `conv_protection.c/h` | 保护 | 软件过流过压检测、FLT 封锁、自动恢复参数（使能/延时/重试） |
| `algorithm_control.c/h` | 算法 | 位置式 PID、准比例谐振 PR（IIR 形式） |
| `algorithm_filtering.c/h` | 算法 | 陷波器、一阶低通、滑动平均等数字滤波 |
| `dashboard.c/h` | 展示 | 表层 UI 接口，向应用层暴露电源状态与可调参数，不直接操作硬件 |

## 3. 与目标工程（本仓库 IH01A）的差异

| 维度 | 源工程 NUEDC_2026A | 目标工程 IH01A | 移植处理建议 |
|---|---|---|---|
| MCU | STM32G474VCT6，Cortex-M4F，有 FPU / DSP 指令 | STM32G071CBUx，Cortex-M0+，**无 FPU、无 DSP** | 浮点运算改定点（Q10/Q15），可参考 `MiaoUI/core/ui.c` 里 `UI_Animation()` 的 Q10 写法 |
| PWM | HRTIM1 五个半桥，~217 ps 分辨率 | 无 HRTIM，常用 TIM1 互补通道 + TIM14/TIM15 | `conv_pwm.c` 需整体重写为 TIM 版 |
| ADC | 4×ADC12 硬件触发同步 + DMA 循环 | 单 ADC + DMA | 触发源改为 TIM 更新/比较事件，通道数按硬件裁剪 |
| 屏蔽/中断优先级 | FreeRTOS + CCM SRAM 放实时代码 | **当前无 RTOS**（框架层为轮询式） | MainLoop 放定时器中断或主循环，暂不引 RTOS |
| 显示屏 | SH1106 4 线 SPI | SSD1315 SPI（`MiaoUI/display/dispDriver.c`） | `dashboard.c` 改成向 MiaoUI 页面提交数据 |
| 菜单 | 源工程自带 `ui_conf.c` | 本工程 `MiaoUI/ui_conf.c`（`[Statistics]` → `PD in` / `System` 占位页） | 仪表盘数据接到这两个页 |
| 依赖头 | `services.h`（框架聚合头，含 UI/PM/时间服务）、`Applications/application.h`、`image.h`、`main.h` | `framework/*.h` 各自独立、无 `Applications` 层、`MiaoUI/images/image.h`、`Core/Inc/main.h` | 逐个替换为窄头文件，不要把聚合头搬过来 |
| `armdsp.h` | 6 个文件 `#include <armdsp.h>` | — | **源仓库里并不存在此文件，代码也没有任何 `arm_*` 调用**，按遗留用 include，可直接删掉 |

## 4. 当前接入状态（重要）

- 目录已放入 `Firmware/APP/PowerControl/`，**没有加入 Keil 工程（`.uvprojx`），不参与编译**，
  也不保证在当前工程下能编译通过（源工程的文件名/变量名与 G0 外设不匹配）。
- 除本说明文件外，所有 `.c/.h/.md` 与源工程逐字节一致，未改名、未裁剪、未修补 include。
  例外：`stm32g4xx_it.c`、`stm32g474xx_flash.sct` 只在文件头加了"仅作参考"横幅，正文未改。
- 本工程的构建里目前只包含 `MiaoUI/`、`framework/` 与 `Core/`，`PowerControl/` 属纯参考代码。

## 5. 建议的移植顺序

1. `algorithm_control.*`、`algorithm_filtering.*`——纯算法，先去浮点化再进工程；
2. `conv_adc.*`——定标与滤波，替换为 TIM 触发 + DMA 采样；
3. `conv_pwm.*`——HRTIM → TIM1 互补 PWM，含死区；
4. `conv_loop.*`——环路结构与限幅，注意 M0+ 上 PR 的定点精度；
5. `conv_controller.*`——状态机与 MainLoop 时基；
6. `conv_protection.*`——软件保护与自动恢复（硬件 FLT 换成 TIM BREAK / 比较器）；
7. `dashboard.*`——并入 MiaoUI 的 `[Statistics]` 页（`PD in` / `System`）。

## 6. 相关文档

- 源工程总说明：`NUEDC_2026A\README.md`、`NUEDC_2026A\copy_manual.md`、`Docs\A题_AC-AC变换电路.pdf`
- 本目录源文档：`README.md`（信号链与文件职责）
- 本工程：`Docs/FIRMWARE_ARCHITECTURE.md`、`Docs/PORTING_NOTES.md`、`Docs/NEXT_STAGE_PLAN.md`

## 7. 参考文件：RAM 执行速度优化（仅参考，移植后删除）

从源工程额外借来两份文件，用于说明"把热路径放进 RAM 执行"的完整做法：

| 文件 | 源位置 | 示范内容 |
|---|---|---|
| `stm32g4xx_it.c` | `Firmware_0/Core/Src/` | `RAMFUNC` 标注 ADC-DMA 半传输中断 → 直接调用 `MainLoop()`（开关频率级热路径）；故障处理里 `PWM_Stop()` + `NVIC_SystemReset()` 的兜底写法 |
| `stm32g474xx_flash.sct` | `Firmware_0/MDK-ARM/` | 分散加载：`.ccm_text` 进 CCM SRAM（`0x10000000`）、512 B 向量表进 CCM（`0x10007E00`）、`ram_ro`/`ram_data` 进 SRAM2、栈置顶 |
| `README_RAM_EXEC.md` | 本仓库 | 该实践的动机、三处措施、代价与坑、G071 上的移植改法与"卸磨杀驴"清单 |

要点：源工程 240 MHz / Flash 5 WS，把中断向量表与 `MainLoop()` 搬进 CCM SRAM 后免掉取指等待；
配套 `RAMVAR`/`RAMRO` 把环路系数与常量池放在 SRAM2。**本工程 G071 没有 CCM SRAM、主频仅 64 MHz、
Flash 等待少，收益很小，暂不建议实施**（详见 `README_RAM_EXEC.md` 第 4 节）。

这三个文件**都不属于 PowerControl**：中断向量归 `Core/Src/`，链接分区归 `MDK-ARM/`，且没有跨 MCU 复用的价值。
移植消化完成后应按 `README_RAM_EXEC.md` 第 5 节删除，真正落地到本工程的只有
`Core/Src/stm32g0xx_it.c` 的 `RAMFUNC` 标注与 `MDK-ARM/Ld/*.sct` 的分区改动。
