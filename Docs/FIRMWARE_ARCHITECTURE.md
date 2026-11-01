# IH01A 固件架构与实机基线（2026-10-31）

目标 MCU：STM32G071CBU6，64MHz，128KiB Flash、36KiB SRAM。

## 调度关系

```text
main()
  HAL/CubeMX peripherals init
  MX_USBPD_Init()
  APP_Tasks_Init()
  USBPD_DPM_Run()               [ST 非 RTOS；内部无限循环]
    CAD / PE (按自身定时判断)
    USBPD_DPM_UserExecute()     [CubeMX USER CODE]
      APP_Tasks_RunOnce()
        CoroOS_RunOnce()
          thread_input_pm       [10ms 键扫描，50ms PM]
          thread_ui             [首次等待 250ms，此后20ms UI]
        SSD1315_Display_Service()  [最多传输 128B 一页]
```

USB-PD 的 1ms 定时仍由 `SysTick_Handler` 中原有 `USBPD_DPM_TimerCounter()` 维护；`APP_Tasks_RunOnce()` 不可放到 `USBPD_DPM_Run()` 之后。用户已实机确认该结构下屏幕显示、按键和 UI_OFF 可运行。诊断此问题时引入的四个临时计数/阶段变量已删除。

## 外设与 UI

- SSD1315 128×64：SPI1 PB3=SCK、PB5=MOSI、PA15=硬件 NSS/CS、PB4=D/C；初始 `U8G2_R2` 为用户修正后的方向。屏幕外部 VCC，初始化禁用电荷泵（`8D 10`）。
- OLED RES# 与 STM32G071 PF2/NRST 共网，不使用独立复位 IO。应保持 NRST_MODE 的 Reset input/output 配置，并检查外部 VCC 与 NRST 的启动时序。
- 四键：L(PA12)、R(PA11)、ENTR(PC7)、EXIT(PD3)，全部低有效。基础交互与 UI_OFF 用户已上板验证。
- MiaoUI：Main、Settings、Statistics、About；Statistics/PD in 是真实 Source PDO 只读快照，Main 仍无加热控制。UTF-8 路径已启用，中文字模未引入。
- PM：来自 PB02B/DPC27A，最高仅 UI_OFF，不停止 USB-PD 和安全外设。
- coroOS：Protothreads 风格无栈协作调度，跨 `THRD_DELAY` 需要保持的局部数据应使用 static/外部存储。

## 内存与执行时延

- CubeMX `HeapSize=0x400`、`StackSize=0x400`；启动汇编设置一致。用户实测零堆时运行库初始化卡住，配置 1KiB 堆后基础功能恢复。最小/峰值需求未实测，不能把 `USBPD_PE_GetMemoryConsumption()` 当成实时堆占用。
- u8g2 帧缓冲、活动/待发送帧各 1024B，共 3072B 静态屏幕缓冲。
- SSD1315 初始化也改用 DMA 并同步等待完成，正常页面刷新则异步推进；仍须测量最坏执行时长与 PD 状态机服务间隔。在未测出负载和时序裕量前不主张接入更多阻塞性任务。
- `BSP/Console` 精简 printf 使用 RAM 日志环，保留必要的 C 库重定向和退出诊断；移除的仅是本轮问题专用 PD/UI 调度计数器。

## 安全边界

仍为**控制面板联调版本**，未建立加热负载所需的硬件过流保护、温度/供电联锁、PWM 输出安全状态机或受控启停流程。软件和 GUI 不启动加热 PWM。

下一阶段：`Docs/NEXT_STAGE_PLAN.md`。

## SPI1 DMA runtime transport

CubeMX provides SPI1 TX DMA1_Channel1 and the IRQ. Display init now uses the same HAL_SPI_Transmit_DMA entry and waits for
completion; it no longer calls HAL_SPI_Transmit. Thereafter the display service starts
DMA commands then DMA page data as separate transactions; ISR callbacks
record completion and no transfer starts from IRQ context. The
SSD1315_Display_Service advances the state during the PD user callback.
Command and framebuffer storage remain valid while DMA is in flight.
At most one DMA operation starts in each call. Verify timing and NSS/DC
with a logic analyzer before treating USB-PD interleaving as validated.

## PD Source PDO static snapshot

`USBPD_DPM_SetDataInfo(USBPD_CORE_DATATYPE_RCV_SRC_PDO)` copies valid
4-byte-aligned Source PDO data (max seven objects) into
`APP/pd_source_stats.c`; detach, stack stop and reset clear the mirror.
`IH_Statistics_Poll()` formats fixed-size row labels at 20ms UI time,
rather than doing formatted output inside time-critical PD callbacks.
The Statistics/PD in `UI_PAGE_TEXT` uses seven statically allocated
`UI_ITEM_WORD` rows and widens only that page's text clipping area.
No heap allocation or UI page rebuilding is involved.

## PowerControl 采样与中断（2026-11-01，尚未板端验收）

`PowerControl_RelocateVectors()` 在 SysInit 阶段写 SRAM VTOR。原始
Flash 向量表不变，`DMA1_Channel2_3_IRQHandler` 迁至
`APP/PowerControl/power_control.c` 并标记 `.ih_ramfunc`。
TIM6 TRGO→ADC1 8 Rank→DMA1 Channel2 双帧（16 halfwords）→
HT/TC→控制状态机；中断执行期间拉高 ISR_TIME(PF1)，结束拉低。
主循环与 USBPD / GUI 的调度独立于此 20 kHz 采样域。

状态机 STOP/MONITOR/FAULT；PWM_EN=0、TIM1 MOE=0，
移相/调频/间歇现阶段仅计算 Q15 请求，不写 TIM1 比较或使能寄存器。
软件算法不含浮点运算。ADC 顺序、基准、电气限制、SRAM 分区见
`APP/PowerControl/README.md`。

## 2026-11-01 PowerControl 结构重整

已按 `APP/PowerControl/{conv_adc,conv_controller,dashboard}.c/h` 拆分旧单体实现，并在 Keil 中建立独立 `PowerControl` Group。G474 HRTIM/float 旧源码归档至 `APP/PowerControl/Legacy/*.legacy.*`，没有加入编译。向量表 SRAM 拷贝和 `SCB->VTOR` 切换由 `Core/Src/main.c` 实现，不再作为 PowerControl API；DMA1 Channel2/3 ISR 仍在 `conv_controller.c` 并放进 SRAM。

`Statistics → System` 显示七路 ADC 引脚电压（2.5V 标称参考）和经出厂 30/130°C、3.0V 校准值折算的 MCU 温度，100ms 更新原始 ADC 采样值；具体转换公式、通道及安全边界见 `APP/PowerControl/README.md`。当前无 PWM 控制输出。
