# IH01A 基础固件架构（第一阶段）

目标芯片：STM32G071CBU6，64 MHz，128 KiB Flash / 36 KiB RAM。
原始 CubeMX HAL/LL 工程、USBPD、ADC/COMP、TIM1/TIM15 均保持原配置。

## 主循环与执行顺序

```text
MX_USBPD_Init()
APP_Tasks_Init()
while (1):
    USBPD_DPM_Run()       <-- 每轮首先执行，绝不放入会等待的 UI 协程
    APP_Tasks_RunOnce()
        coroOS:
            thread_input_pm (10ms，含 PM 50ms poll)
            thread_ui       (20ms，首次延迟250ms再初始化OLED)
        SSD1315_Display_Service()   <-- 每轮最多发 1 页（128B）
```

- `Peripheral/Time` 使用 HAL_GetTick() 作为 1ms 时基。原 `SysTick_Handler` 中 `HAL_IncTick()` 与 `USBPD_DPM_TimerCounter()` 不变。
- `MDK-ARM/Scheduler/coroOS` 为无栈协作式 Protothreads 风格，不支持抢占，跨 `THRD_YIELD/DELAY` 的变量必须 static 或来自外部句柄。
- `BSP/Buttons` 4键30ms稳定消抖：L(PA12)=UP、R(PA11)=DOWN、ENTR(PC7)=确认、EXIT(PD3)=返回，均低有效；L/R 约400ms后每100ms重复，ENTR/EXIT松手触发。休眠唤醒键事件被吞，不会误触菜单。
- `APP/MiaoUI` 来自 DPC27A 的 Q10 定点版本，基于裁剪 u8g2；SSD1315 128×64 `U8G2_R0`。根菜单仅四项：Main（主界面）、Settings（设置）、Data（数据）、About（关于）。主界面/数据页是不可控制加热器的占位页；中文字库与通用弹窗尚未实现。
- `APP/framework` 使用 DPC27A 的 PB02B 式真实冻结倒计时，注册 OLED 和 MCU 节点，`PM_MAX_SLEEP_DEPTH` 仍为 `UI_OFF`，只发 OLED 0xAE/0xAF 并停止菜单绘制；不改变 USBPD、PWM、ADC、COMP 的运行状态。

## 显示硬件和供电约束

SSD1315 四线 SPI：PB3=SCK、PB5=MOSI、PA15=硬件NSS/CS、PB4=D/C。
`BSP/Display/ssd1315_display.c` 初始化时明确写 `8D 10` 禁用电荷泵，**绝不使用 `8D 14`**。
OLED **VCC 由外部提供**。默认硬件约定：**SSD1315 RES# 与 STM32G071 PF2/NRST 直接共网**，无额外屏幕复位 GPIO。STM32G0 的 NRST_MODE 应保持出厂默认的 Reset input/output：外部复位和内部复位均使 NRST 拉低，内部复位低脉冲最短 20µs（ST AN5096）。如果选项字节改为 Reset input only/GPIO，就无法用内部复位同步复位 OLED。
共网复位无法在 MCU 正常执行时单独重置 OLED，且仍须确保 SSD1315 的 RES# 电压门限、复位脉宽和外部 VCC 上电释放顺序满足实际面板要求。
固件在 MCU 启动后协作式等待 250ms 再初始化，这并不保证晚到的外部 VCC 在释放复位前已稳定；需测量实机电源与 NRST 波形。PCB 连线本次尚未独立核查。
u8g2 和 DMA 前的帧拷贝区共占：1024B u8g2帧缓冲 + 1024B活动帧 + 1024B待发帧 = 3072B。
首版每轮使用阻塞 HAL_SPI_Transmit 发送一页，尚未使用 SPI DMA；必须在实机确认 PD 时序最坏情况。

## 安全边界

**本次固件不允许 GUI/PM 启动加热器。** 原有 PWM_EN PC6 上电低电平、不调用 TIM1 输出使能。
TIM1、比较器、过流检测与 USBPD 仍属于原始 CubeMX/USBPD 工程，后续加热算法须另行做安全状态机与硬件保护验证。

## u8g2 精简版链接边界

`u8g2_buffer.c` 的 PBM/XBM 帧缓冲导出函数依赖 upstream 的 `u8x8_capture.c`，IH01A 不使用该图像导出功能。默认不定义 `U8G2_ENABLE_BUFFER_CAPTURE`，其四个导出函数及头文件声明不参与编译，因此不再引入六个 `u8x8_capture_*` 未定义符号。SSD1315 的帧缓冲绘制、页面传输和显示刷新均不受此开关影响。
