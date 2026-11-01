# IH01A 固件（STM32G071CBU6）

基于 STM32CubeMX + Keil ARMClang 6，已完成用户实机基础功能验证：**OLED 显示、四键操作、UI_OFF 休眠/唤醒**。此结论不涵盖 USB-PD 全协议压力测试与加热功率控制。

## 模块

- `APP/app_tasks.c`：coroOS 协程，UI 首次延迟 250ms 初始化。
- `APP/framework/`：从 PB02B/DPC27A 迁移的 PM（最大休眠等级仅 UI_OFF）。
- `APP/MiaoUI/`：定点 MiaoUI，根菜单 Main / Settings / Statistics / About；Statistics → PD in 显示来自协议栈的 Source PDO。
- `BSP/Display/`：SSD1315、SPI1，屏幕外部供电、内部电荷泵禁用（`8D 10`）；屏幕 RES# 与 MCU NRST 共网，默认 `U8G2_R2`。
- `BSP/Buttons/`：四键低有效与去抖、唤醒首键屏蔽。
- `BSP/Console/`：PB02B `mini_format` 与整数版 `printf/snprintf` 适配。
- `Peripheral/Time/`：`HAL_GetTick()` 1ms 时基。
- `MDK-ARM/Scheduler/`：无栈 coroOS。
- `ThirdParty/u8g2/`：精简 u8g2，已支持 UTF-8 解码与 BMP Unicode 字形查找，**尚未加入 CJK 字库**。

## 真实调度入口

ST 非 RTOS `USBPD_DPM_Run()` 自带无限循环，**不会返回到 `main()`**。因此必须在
`USBPD/Target/usbpd_dpm_user.c` 的 `USBPD_DPM_UserExecute()` USER CODE 内
调用一次 `APP_Tasks_RunOnce()`。此调用在 PD CAD/PE 任务之后执行。
OLED 运行时以 DMA 分事务发送命令和 128B 页面；初始化也统一调用 DMA，等待其完成后继续。需要验证 PD 事件的最坏时延。

## 经实机核实的堆要求

- 初始配置：`ProjectManager.HeapSize=0x000`；用户调试时程序在 C 运行库初始化路径卡住。
- 当前配置：`Firmware/Compact_Induction_Heater.ioc` 与 `MDK-ARM/startup_stm32g071xx.s` **均为 `Heap_Size=0x400`（1024 B）**；用户报告可正常显示/操作/休眠。
- 栈仍为 `0x400`（1024 B）。**保持现有配置，不再推荐盲目改成 0x1000**；后续通过 map 文件、堆高水位与栈水位确定安全裕量。动态分配源头仍须通过调用链进一步确认。

## 格式化与调试支持

`BSP/Console/mini_format.c/.h` 从 PB02B 移植，`mini_stdio.c` 接管
`printf`、`snprintf` 等整数/字符串输出，不支持 `%f`、`%p`、精度以及
`long long`。`MDK-ARM/retarget.c` 的 `__use_no_semihosting`、
`_ttywrch`、`fputc` 和 `_sys_exit` 仍承担**运行时必需的重定向/错误出口**；
`g_runtime_log` 为 `printf` 后端，而非已移除的临时调度跟踪计数器。

## 安全约束

GUI/PM 不使能加热 PWM，原有 TIM1/COMP/ADC/UCPD 的配置暂不改动。
未建立过流/过温/失电安全联锁及闭环前，**不得使用实际加热线圈进行带功率调试**。

下一阶段见 `Docs/NEXT_STAGE_PLAN.md`。

## OLED SPI1 DMA and Burn-in Test

Although SPI1 DMA1_Channel1 was already initialized by CubeMX, the old
SSD1315_Display_Service used blocking HAL_SPI_Transmit. Runtime page
commands, 128-byte page payloads, contrast and power-save commands now
use HAL_SPI_Transmit_DMA with a persistent command buffer and per-transfer
completion/error flags; D/C changes only after HAL reports completion.
u8g2 initialization sends commands through the same DMA entry but waits for
IRQ completion before returning; runtime does not block for completion.
Keil should no longer need HAL_SPI_Transmit for OLED. Keep CubeMX
DMA1_Channel1 IRQ and SPI1 IRQ enabled.

Settings > Display now includes "Burn-in Test" before "Screen Flip".
The page fills all 128x64 pixels with white and ANY L/R/ENTR/EXIT event
exits back to the menu. **Avoid prolonged full-white exposure**; OLED
aging and permanent retention are possible. No fixed timeout was added.

Buttons sampled every 10ms: debounce 60ms, L/R initial-repeat delay
(DAS) 800ms and repeat interval 200ms. ENTR/EXIT remain release-based;
the longer 16-bit hold counter avoids repeat freeze after ~2.5s.
L / UP now decrements integer/decimal parameter widgets, R / DOWN increments.

## Statistics / PD in: real Source Capabilities

`APP/pd_source_stats.c` mirrors received `USBPD_CORE_DATATYPE_RCV_SRC_PDO`
from `USBPD_DPM_SetDataInfo()`; up to 7 objects, 28 raw bytes.
`USBPD_DPM_UserCableDetection`, stack start/stop and hard reset clear
stale data. The `Statistics -> PD in` PAGE_TEXT page owns seven static
32-byte row labels, refreshed at 20ms UI polling only if the revision
changed. The page expands text clipping while leaving other settings
pages untouched. Sample:
```text
[Return]
1. Fixed 5.0V 3.0A
2. Fixed 9.0V 3.0A
```
Absent/unreported Source Capabilities: `No PD Source` on the first
row and blank remaining rows. It reports advertised capabilities, NOT
the negotiated RDO or measured output voltage. `%f` and heap use are
not required. Unknown APDO subtypes are labeled but not fabricated.

## PowerControl v1（2026-11-01，待实机验证）

已将 `APP/PowerControl/power_control.c/h` 作为仅监测的数字电源移植基线加入 Keil。
TIM6 TRGO 驱动 ADC1 八通道扫描，DMA1 Channel2 circular 双帧，HT/TC
中断运行在 SRAM 并在 ISR_TIME(PF1) 输出高电平测量 ISR 时间；
滤波与三种模式命令均使用定点数，功率输出始终关闭。
中断向量复制到 SRAM `0x20008F00`；热代码置于 SRAM
`0x20008000`。ADC 标称参考 2.5V，ISR_LOAD 的可测负载约 0~75.8%，
更高占空率会饱和。完整 ADC Rank 顺序见
`APP/PowerControl/README.md`。旧 G474 源文件仍作为参考，未加入编译。

## PowerControl 模块拆分与采样页面（2026-11-01）

Keil 工程已增加独立 PowerControl 分组：`conv_adc.c`（8 Rank ADC 双帧 DMA 与固定点 mV/温度换算）、`conv_controller.c`（ADC DMA ISR/ISR_TIME/故障状态机）、`dashboard.c`（Statistics → System 八路只读文本）。原 G474 文件已加 `.legacy.*` 后缀归档到 Legacy，不参与编译。向量表重定向在 `Core/Src/main.c`。详细使用和计算公式见 `APP/PowerControl/README.md`。本次未在 Keil 构建，需板端验证。
