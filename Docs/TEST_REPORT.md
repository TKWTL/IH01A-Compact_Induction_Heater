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

## 2026-11-01 unified OLED DMA + Source PDO dynamic PAGE_TEXT

Implementation pending Keil/hardware verification:
- All SSD1315 SPI transfers use HAL_SPI_Transmit_DMA. Initialization
  waits for the DMA TxComplete callback with timeout; runtime transfer
  continues asynchronously. No HAL_SPI_Transmit reference in the OLED
  driver. Verify DMA1_Channel1 IRQ is serviced during startup.
- `USBPD_CORE_DATATYPE_RCV_SRC_PDO` updates a static 7-element snapshot.
  Statistics -> PD in shows Fixed, Variable, Battery, PPS information from
  actual Source_Capabilities, not guessed/current contract. Detach,
  stack reset and hard reset clear stale PDO entries.
- Seven fixed row labels, 32 bytes each; `PAGE_TEXT` clipping widened
  locally. Initial/no PD message is `No PD Source`, other rows blank.
- Host regression source `test_pd_source_stats.c` added. No ARMClang
  build, host test run or target PD analyzer results claimed yet.

## 2026-11-01 PowerControl v1（代码接入，目标验证待完成）

- 已按 CubeMX 的 Rank 1..8 映射 CT_RMS、IBUS、NTC_POT、VBUS、NTC_SYS、VBIAS、ISR_LOAD、MCU 温度；ADC 使用 TIM6 TRGO 触发与 DMA1 CH2 循环双帧。
- DMA1 CH2/3 中断入口转移到 PowerControl 源文件并标记 `.ih_ramfunc`；原生成文件的重复实现删除。Keil scatter 将热代码分配到 SRAM，把 256B 的 VTOR 副本放在 0x20008F00。尚未运行 ARMClang 编译/链接，必须检查 SRAM 块溢出与 map。
- 用 ISR_TIME PF1 输出 ISR 执行高脉冲，ADC 的 ISR_LOAD 信号仅能测量约 75.8% 以内的负载（VREF 2.5V、GPIO 高 3.3V）。需验证真实 VREF+ 电压和 RC 滤波时间常数。
- 状态机中只读观察 ADC，定点 Q4 滤波/Q15 移相、调频、间歇请求，PWM_EN 和 TIM1 MOE 始终为 OFF，无可用加热闭环。
- **待板端测试**：Keil compile/link；DMA HT/TC 每组正确 8 个数据；ADC OVR/TE/积压故障；PD 协商与 OLED DMA 在 20 kHz ADC ISR 负载下仍可靠；示波器验证 ISR_TIME 波形、TIM6 20 kHz 与 TIM1 未使能；内存 map/堆栈余量；上电 VREF+ 实测。

## 2026-11-01 PowerControl 采样可视化重构（尚未 Keil/实机验收）

- 用户已在上一个实现测得 ISR_TIME 高脉冲约 4.5µs、20kHz，推算 ISR 占用约 9%；理论 ISR_LOAD=0.297V ≈487 LSB（2.5V 12bit），不是新的移植版实测数据。
- 已将重定向代码从单体 PowerControl 移到 main.c 的 `RelocateVectorTable()`。Keil Group 单独列出 PowerControl/conv_adc.c、conv_controller.c、dashboard.c。
- Statistics → System 新增 8 行只读采样数据：7 路 ADC 原始采样码转换后的引脚电压，MCU 温度按 G071 双点工厂校准值（30/130°C、校准 VREF 3.0V）和实际标称 2.5V 折算；100ms 文本更新。
- 原 G474 代码以 .legacy.* 后缀保留在 Legacy 子目录，未编译；原单体 power_control.c/h 删除。TIM1 和 PWM_EN 保持安全禁用。
- **未验证**：Keil/ARMClang 编译链接、RAM 向量表实际起始地址、PF1 新脉宽、ADC Rank 数据和温度正确性。下一步必须先验证这些项目再谈调功。

## 2026-11-01：system/time_service 与 RTC Uptime（待实机测试）

- 读取 Collar-Radio 旧系统管理、系统统计和 RTC 时间封装，
  裁剪为新的 `framework/system.c/h`、
  `framework/time_service.c/h`（后者避免覆盖 C 标准 `time.h`）。
- 核实 IH01A `MX_RTC_Init()` 每次启动设置 2000-01-01 00:00:00、
  LSE 32768 Hz、分频 127/255、24 小时日历。
  无备用电池导致失电后时间丢失符合设计预期；软件重启也从零开始。
- Settings/System 在 Sleep Options 后追加立即生效的
  System Reset（`UI_ITEM_ONCE_FUNCTION`）。
  Statistics/System 首行追加 Uptime（`Nd HH:MM:SS`），其后原 ADC
  八路不变，RTC 读失败显示 `Uptime: RTC error`。
- 新增主机单元测试源码：按 2000 闰年、2001 非闰年、
  跨日跨月、相对秒数、格式化及 RTC 读取失败模拟进行检查。
- **尚未执行 Keil/实机验证**：RTC 每秒是否递增、
  长时间运行/跨日、UI_OFF 恢复、Soft Reset 后零点重新开始；
  还需检查完整 Keil 编译和 MPU/中断向量布局。

## 2026-11-01 DMA ISR duplicate symbol hotfix

用户 Keil ARMClang 6.24 链接时报 L6200E：
`DMA1_Channel2_3_IRQHandler` 同时定义在 `conv_controller.o` 和
`stm32g0xx_it.o`。仓库检查确认 CubeMX 生成入口已再次出现。
修复方法：在 `stm32g0xx_it.c` 的 `USER CODE BEGIN PFP` 中
以 `__WEAK` 声明该函数，使生成的实现为弱符号，而
`APP/PowerControl/conv_controller.c` 的 SRAM ISR 保持强定义。
已用本地 Clang 验证这种“先强声明、再 weak 声明、最后定义”的组合
生成弱符号，可与另一目标文件中的强定义成功链接。
**尚未在目标 Keil ARMClang 6.24 工程中重编译验证**；下次 Rebuild 后
检查没有 L6200E，并用 .map 确认入口在 `.ih_ramfunc` 和 SRAM VTOR 中。

## 2026-11-01 PD sink/ADC VBUS framework (code only)

- Added PD Input module and pure Fixed PDO selector; patched DPM
  GetDataInfo(SNK_PDO), SetDataInfo(RCV_SRC_PDO),
  SNK_EvaluateCapabilities and event callbacks. Request policy is one
  automatic highest-voltage Fixed SPR profile per CC attach, <=20V with
  present PD3.0 minimal sink library and <=3A source-limited current.
  EPR/AVS/PPS/32V are NOT supported.
- Patched `USBPD_PWR_IF_Init` placeholder ERROR to OK; power voltage
  read callbacks now obtain `VBUS ADC pin voltage *16` (mV) instead
  of zero; current remains uncalibrated. VBUS in Statistics/System now
  reports actual divided-bus estimate.
- Added `Settings->PD->Retry PD` and read-only input/BUS/request
  indicators. No automatic recurring requests: PE may send a repeated
  Request upon source retransmission/recovery, but DPM policy reuses
  previously selected PDO rather than auto-upgrading it.
- Pure host tests cover fixed voltage ranking, RDO bit fields, source
  current cap, PPS/EPR/32V exclusions and malformed PDO lengths.
- **Not yet target verified**: Keil compile/link, actual CC Rd
  paralleling, physical dual-input isolation, initial sourcecap order,
  5/9/15/20V negotiated transitions, manual retry on a failed contract,
  VBUS voltage trace and >32V input overvoltage handling. Do not turn on
  heater PWM before all are qualified.
