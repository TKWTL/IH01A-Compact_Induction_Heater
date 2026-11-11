# IH01A Flash 链接占用审计与压缩路线（2026-11-11）

## 1. 基线与口径

基线文件：用户提供的 `Compact_Induction_Heater(1).map`（Arm Compiler for Embedded 6.24 / armlink）。这份映像已包含单套 Fusion Pixel 中英文字库（`font_fusion_pixel_12_zh.o`），不包含旧 `fonts.o`，但**不包含本次 TEXT 选中框高度微调后的重新编译结果**。

按本次 MAP 链接配置，`LR_IROM1` 大小上限为 `0x20000 = 131072 B`（128 KiB）。这里统计的是配置的 Flash 区间；未扣除未来引导程序、OTA、保留页等可能额外要求。

| 项目 | MAP 原值 | 字节 | 说明 |
|---|---:|---:|---|
| Flash 加载区 `LR_IROM1` | `0x1BB00` | **113408** | 86.5234% 占用；**剩余 17664 B** |
| `ER_IROM1` | `0x1B45C` | 111708 | Flash 中直接执行的代码和常量 |
| `ER_RAMCTRL` | `0x488` | 1160 | 在 RAM 中执行、由 Flash 提供加载映像的代码 |
| `Total RO Size` | — | 112868 | Code 103672 + RO Data 9196 |
| `Total RW Size` | — | 25512 | RW Data 484 + ZI Data 25028；**不是 Flash 大小** |
| `Total ROM Size` | — | 113352 | Code + RO Data + 已初始化 RW Data |
| 链接加载区对齐填充 | — | 56 | 113408 − 113352 |

`Total ROM Size` 不能与 `LR_IROM1` 混用：后者包含链接布局中实际占据的填充空间。用于判断离 Flash 边界有多远，优先读取 `LR_IROM1` 的 Size/Max。

## 2. 按模块归属的 Flash 大小

以下由 MAP 的 Object Totals / Library Totals 行归类汇总，单位为字节。分组按模块职责人为划分，供找优化热点使用；并非链接器原生分区，少量服务类文件跨越功能边界。

| 分组 | 已链接大小（B） | 主要来源 |
|---|---:|---|
| ST USB-PD FULL 预编译静态库 | **26186** | `USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib` |
| USB-PD 应用策略与外设适配 | **13058** | `pd_input.o`、`pd_diag.o`、`pd_source_stats.o`、`usbpd_* .o` 等 |
| GUI 框架、U8g2、字形与图像 | **24318** | `ui.o`、`ui_conf.o`、`u8g2_*.o`、`font_*.o`、`i18n.o` 等 |
| STM32 HAL / LL / startup | **19907** | HAL 驱动、LL、启动文件 |
| 加热控制与保护 | **13263** | `conv_controller.o`、`conv_protection.o`、`heater_control.o` 等 |
| 核心、BSP 与一般服务 | **14136** | `app_tasks.o`、`usart.o`、`console_cli.o`、系统及设备管理 |
| Arm C 运行库 | **2174** | `printf8.o`、整数运算、内存和字符串运行库 |
| 生成项、填充与零散差异 | **310** | Object / Library Totals 的附加生成与 padding |
| **Grand Totals 的 ROM 大小** | **113352** | 与 MAP 一致 |

两个主要功能域：USB-PD（库 + 策略 + 适配）约 **39244 B（34.6% 加载区）**；GUI（含字库）约 **24318 B（21.4%）**。两个合计约 56%，因此若要为后续功能扩展争取 >6 KiB，单纯减少几条中文译文不够。

### 2.1 大型链接对象

以下数值为 `Code + RO Data + RW Data`，不是源代码文件体积或 Debug 列大小：

| 对象文件 | Flash（B） | 注意 |
|---|---:|---|
| `usbpd_pe_vdm.o` | 5564 | 来自 USB-PD FULL 库 |
| `pd_input.o` | 5022 | 包含 EPR 轮询、PDO 请求策略 |
| `usbpd_pe_snk.o` | 4968 | Sink 状态机 |
| `usbpd_pe.o` | 4546 | 协议策略引擎 |
| `conv_controller.o` | 4264 | 控制、检锅等 |
| `ui.o` | 4058 | MiaoUI 绘制和菜单状态 |
| `font_fusion_pixel_12_zh.o` | **3428** | 字库仅占加载区约 3.0% |
| `stm32g0xx_hal_uart.o` | 3302 | STM32 HAL UART |
| `usbpd_prl.o` | 3278 | PD 协议层 |
| `conv_protection.o` | 3097 | **保护逻辑，不应因空间原因直接裁剪** |
| `stm32g0xx_hal_adc.o` | 3076 | HAL ADC |
| `stm32g0xx_hal_tim.o` | 2976 | HAL TIM |

### 2.2 热点函数和资源

从 Memory Map 的已链接 section 读取：

- `USBPD_PE_StateMachine_SNK` **4564 B**，`PE_StateMachine_VDM` **4184 B**：PD 状态机、结构性大项。
- `Draw_UI` **1980 B**，`PD_Input_EPR_Poll` **1552 B**：应用级热点。
- `font_fusion_pixel_12_zh` **3428 B**：单套 ASCII + 汉字 U8g2 RLE，已完成两种语言复用。
- `i18n.o` **1464 B**：代码 92 B、只读翻译数据 1372 B；后者含 `s_dictionary` 408 B 和 `.rodata.str1.1` 964 B。
- `image.o` **480 B**，`u8g2_font.o` **1324 B**；整个 U8g2 驱动相关模块约 **5171 B**。
- `console_cli.o` **1816 B**、`heater_cli.o` **1827 B**、`heater_cli_parse.o` **654 B**、`pd_diag.o` **1096 B**，适合按产品构建配置审核哪些诊断入口必须保留。**这些大小不能直接相加当作最终可节约空间**，因为存在共享依赖。
- `mc_p.l(printf8.o)` **1250 B**：说明格式化输出带入了运行库，可定向检查多处 `snprintf`。

## 3. 分级压缩路线（先验证再改）

### P0：低风险，推荐首先进行

1. **创建可对比的 Release / Engineering 构建开关**，将 `heater_cli`、`console_cli`、测试命令、冗余 PD 报文格式化与连续日志分别做编译期裁剪实验。调试版仍保留完整诊断能力；正式版不能删除故障记录、硬件联锁和必要的恢复指令。每改变一个开关，重新编译记录 `LR_IROM1` 差值，而不是用整个 .o 大小推断节省。
2. **审计格式化字符串**：集中处理重复 `snprintf`、大格式模板和只用于测试的诊断表；优先使用现有的定点格式化能力。检查可能的字符串池和间接依赖，但不要牺牲运行期可靠性。
3. **保留 Fusion Pixel 单套字库**。已裁剪到 3428 B；下一步若增加汉字，只按真实菜单译文生成子集。进一步压缩 `s_dictionary` 的理论上界也很低（指针表仅 408 B），不应优先牺牲“英文原文为索引”架构。

### P1：有收益但需额外验证

4. **HAL 重量级接口按调用点审核**：UART、ADC、TIM、SPI、I2C 对象中有多个 2–3 KB 模块。优先把**非关键路径**单个接口改为 LL / 直接寄存器实验并前后测量 Flash 差值；不要整体删 HAL，更不要直接改 ADC/PWM/COMP 安全时序。尤其要复核 DMA/回调/中断在实际硬件上的行为。
5. **MiaoUI 精简与选择性绘制**：`ui.o` 4058 B，`Draw_UI` 1980 B；可按未来产品是否使用 ICON、文本弹窗、动画、圆角、图片资源决定分层编译。界面模块比翻译指针表更值得审计。

### P2：高收益潜力但高风险，不作为 IH01A 的默认行动

6. **USB-PD FULL 配置 / 预编译库选型评估**：静态库 26186 B，VDM/VCONN 与 Sink 状态机较大。但 IH01A 的 EPR 与线缆识别依赖相关策略，**不能因为 `usbpd_pe_vdm.o` 达 5564 B 就断言该功能可删除**。需先核查 ST USBPD 中间件 feature matrix、依赖关系、替代库和 EPR 进入/保持/回退流程，再在隔离版本上编译、抓包和高压验证。
7. **编译器优化 / LTO A/B 测试**：固定 armclang 版本、目标、优化等级和链接脚本；逐档测试 `-Oz`、链接期优化的可用性。预编译 PD 库未必参与跨单元优化。逐项检查 ISR 最坏耗时、128 MHz TIM 时基、中断放置、RAM 函数执行、异常及 USB-PD 协议定时，不能只看 Flash 减小。

## 4. 验收建议

目标可暂定为链接加载区 **≤104 KiB（106496 B）**：相较本次 113408 B，需实测减少 **6912 B**，剩余 Flash 可提升至 **24576 B（24 KiB）**。该值是建议的下一阶段预算，不是已经达到的效果。

每项优化均保留相同的构建标志并记录：

| 验收字段 | 必须提供 |
|---|---|
| 固件版本 | Git SHA、armclang 版本、构建目标、完整定义 |
| Flash | `LR_IROM1 Size`、`Total ROM Size` 及变化量 |
| RAM | `Total RW Size`、HEAP/STACK、水位或余量 |
| 功能 | GUI 两语言、字体布局、所有诊断路径、PD/EPR 协商与回退 |
| 安全 | COMP/TIM1 BRK、ADC AWD、16 A 保护、手动恢复、检锅短脉冲 |
| 时序 | 功率 ISR 最坏执行时间、USB-PD 定时、DMA/屏幕刷新 |

**注意**：在不重新编译的情况下，MAP 不能预测裁剪一个组件后节省多少 Flash；高层静态对象大小只是空间分布证据。也不应直接把 `Debug` 列计入 Flash。

## 5. 额外 RAM 观察（非本轮目标）

`RW_IRAM1 Size=0x6298=25240 B / Max=32768 B`，该执行区尚余 **7528 B**；`ER_RAMCTRL` 中有 **1160 B** RAM 中执行代码，另外存在 reset diagnostics 和 vector 专用区。MAP 中保留 `HEAP=8192 B`、`STACK=1024 B`；缩减堆栈仅影响 RAM，不能用来计算 Flash 节省。

## 6. 本轮变更边界

本轮仅将 TEXT 选中框按中英文各自向上/向下扩展 1px，`main` 上不裁剪 PD、检测和保护代码。Flash 压缩方案列为下一阶段实验计划，需要在 Keil 中实测验收。
