# IH01A 基础固件（STM32G071CBU6）

使用原始 STM32CubeMX / Keil MDK-ARM 5.32 工程，在现有 USBPD HAL/LL 基础上添加：

- `APP/app_tasks.c`：coroOS UI/PM协作任务。
- `APP/framework/`：PB02B/DPC27A 移植的 UI_OFF 电源框架。
- `APP/MiaoUI/`：DPC27A 定点整数 MiaoUI，英文最小菜单。
- `BSP/Display/`：SSD1315 128×64 4-wire SPI，外部 VCC，电荷泵禁用(8D 10)；OLED RES# 与 STM32 PF2/NRST 共网，无复位 GPIO。
- `BSP/Buttons/`：四个低有效按键。
- `Peripheral/Time/`：1ms HAL tick。
- `MDK-ARM/Scheduler/`：无栈 coroOS（coroOS.c/.h 与 Keil 工程文件同级组织）。
- `ThirdParty/u8g2/`：裁剪版 u8g2（BSD-2-Clause）。

在 Keil 中打开 `MDK-ARM/Compact_Induction_Heater.uvprojx`；
新增 C 文件已登记至 IH01A/APP、IH01A/BSP+Time+Scheduler、IH01A/u8g2 三组。
原有 CubeMX 生成区仅在 main.c USER CODE 中挂接 APP_Tasks；后续重新生成 CubeMX 代码时须保留 USER CODE。

**约束：** GUI/PM 不启动加热 PWM；USBPD_DPM_Run 每轮先执行；PM 深度仅 UI_OFF。
本次不宣称已完成 Keil 编译或上板，详见 `Docs/TEST_REPORT.md`。

根菜单：**Main / Settings / Data / About**（主界面／设置／数据／关于）。Main 与 Data 尚无实际业务功能。共享 NRST 为默认硬件约定，等待网表及实机时序核实。

链接说明：裁剪 u8g2 的 PBM/XBM 导出接口使用 `U8G2_ENABLE_BUFFER_CAPTURE` 条件编译，默认关闭，避免依赖未引入的 `u8x8_capture.c`。如需导出图像，需先加入与库版本匹配的 capture 实现。
