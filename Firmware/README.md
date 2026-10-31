# IH01A 固件（STM32G071CBU6）

基于 STM32CubeMX + Keil ARMClang 6，已完成用户实机基础功能验证：**OLED 显示、四键操作、UI_OFF 休眠/唤醒**。此结论不涵盖 USB-PD 全协议压力测试与加热功率控制。

## 模块

- `APP/app_tasks.c`：coroOS 协程，UI 首次延迟 250ms 初始化。
- `APP/framework/`：从 PB02B/DPC27A 迁移的 PM（最大休眠等级仅 UI_OFF）。
- `APP/MiaoUI/`：定点 MiaoUI，根菜单 Main / Settings / Data / About；主界面/数据仍为占位。
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
OLED 采用每次最多一页 128B 的 SPI 发送，但仍是阻塞 `HAL_SPI_Transmit`，需要验证 PD 事件的最坏时延。

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
