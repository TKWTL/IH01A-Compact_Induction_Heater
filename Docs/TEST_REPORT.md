# IH01A 集成测试记录（截至 2026-10-31）

## 实机已验证：用户反馈

| 项目 | 结果 | 证据范围 |
|---|---|---|
| Keil 构建并在 STM32G071 运行 | 已能运行 | 用户已烧录并调试；完整日志/Flash-RAM map 未收集 |
| SSD1315 OLED 显示 | 通过基础功能 | 已正常显示；显示方向由用户修正为 `U8G2_R2` |
| 四键/UI 操作 | 通过基础功能 | 用户报告可正常操作 |
| UI_OFF 休眠/唤醒 | 通过基础功能 | 用户报告休眠正常 |
| C 运行库零堆配置 | 不通过 | `HeapSize=0` 时启动卡住，调用栈涉及 `_init_alloc/_RT...` |
| C 堆 `0x400`（1KiB） | 通过基础启动 | `.ioc` 与 Keil 启动汇编一致；当前可运行，不代表压力工况的裕量已足够 |
| USBPD 与 coroOS 共存 | 基础运行通过 | PD `UserExecute` 钩子驱动调度；多档 PD 协商与极限时序尚未测 |

## 尚未完成的验收

| 类别 | 待验证内容 |
|---|---|
| 内存 | map / 静态 RAM；堆高水位及栈高水位；连续操作与多次插拔后余量 |
| USB-PD | 5/9/12/15/20V 支持档位按实际 PDO 检查；连接、断开、重协商及异常恢复；协议分析仪验证 |
| 时序 | UI/SPI 最长阻塞时间及 USBPD 状态机服务间隔；充电头/供电波动下的表现 |
| OLED 电气 | NRST 共网和外部 VCC 时序、SPI NSS/DC/数据波形、电荷泵命令核对 |
| PM | 不同休眠超时、多次唤醒、按键保持、USB-PD 事件发生于 UI_OFF 时的行为 |
| 加热安全 | PWM_EN 默认关闭；电流、电压、温度采样标定与硬件故障闭锁，均待设计/测试 |

## 已解决的问题（保留回归依据）

1. `u8g2_buffer.o` 因 PBM/XBM 捕获模块缺失而报 `L6218E`：精简版关闭相关导出接口。
2. ArmClang `BKPT 0xAB` / C 运行库启动问题：提供非 semihosting 重定向；实测确认零堆不可启动、当前 1KiB 堆配置可运行。**不能仅据此断定动态分配由 USB-PD 导致**。
3. `mini_stdio.c` 的 `putchar` 宏重定义编译失败：通过 `#undef putchar`、`putc` 适配修正。
4. `USBPD_DPM_Run()` 为非 RTOS 无限循环，原 `main()` 后置 coroOS 语句不可达：改从 `USBPD_DPM_UserExecute()` 的 USER CODE 调度。
5. OLED 方向：用户已在项目中将初始方向更正为 `U8G2_R2`；本次清理不覆盖该修改。

已删除用于上述调度定位的 `g_ih01a_*` 临时监测变量；用于 mini_printf 的 `g_runtime_log` 和致命退出记录保留。

本表中“通过”只代表用户明确报告的基础实机操作，不代表功率加热、PD 边界工况或系统可靠性验证已经完成。

## 2026-10-31 DMA / display test / input follow-up (pending target validation)

Code inspection showed SPI1 TX DMA configured as DMA1_Channel1, but
SSD1315_Display_Service used blocking HAL_SPI_Transmit for commands and
pixel data. Runtime transmit is now asynchronous DMA via
HAL_SPI_Transmit_DMA, using stable source buffers, HAL Tx complete/error
callbacks, nonblocking page-state progress, and a timeout/error reset.
The short u8g2 initialization commands remain synchronous.
**Keil build / SPI waveform / PD time-budget verification not yet run.**

DPC27A's Burnin_Page was reviewed and adapted for the IH01A Display
submenu (before Screen Flip): immediate 128x64 all-white framebuffer,
any new key action exits. Manual testing must avoid prolonged full-white
output due to OLED pixel aging.

Input timing changed from 30/400/100ms to 60/800/200ms
(debounce/DAS/repeat). ParameterSetting_Widget now handles UI_ACTION_UP
with a negative step so L decrements. 16-bit held_ticks avoids earlier
saturation stopping repeated actions after a long hold.
