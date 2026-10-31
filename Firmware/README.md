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

## Keil C 库与诊断

Keil 工程选择 ARM Compiler 6.24 + 标准 C 库，新增 `Firmware/MDK-ARM/retarget.c`
明确导出 `__ARM_use_no_argv` / `__use_no_semihosting`，防止无串口设备在
`_ttywrch` 处通过 `BKPT 0xAB` 调用调试器。
`_ttywrch`、`fputc` 不阻塞，只把最近 128 个字符保存到
`g_runtime_log`；在 Keil Watch 中同时观察 `g_runtime_log_head`、
`g_runtime_log_count`、`g_runtime_exit_seen`、`g_runtime_exit_code`。
`_sys_exit` 为不可恢复错误出口，记录退出码后停止，以免错误状态继续运行。
**请通过 Call Stack 查找最初调用 _ttywrch 的函数，而非据此认定问题已排除。**

Keil Sources 分组 `IH01A/APP/framework` 已独立于 `IH01A/APP`，
文件物理路径仍是 `Firmware/APP/framework`，共 7 个 PM 源文件。

## Mini printf (BSP)

`BSP/Console/mini_format.c/.h` copies PB02B/Bootstrap's small integer
formatter. `mini_stdio.c` supplies global `printf`, `vprintf`,
`snprintf`, `vsnprintf`, `puts`, `putchar`, `fprintf`,
and `vfprintf`, so the project no longer pulls in the full libc
floating-point formatters for those functions. `printf` output is saved
to `g_runtime_log` in RAM by `retarget.c`; it is not sent to UART.
Supported specifiers are `%% c s d i u x X` (width, 0/- and one l).
**No %f, %p, precision, * widths or long long**. Replace such formats
with integer/scaled-integer conversions before using this library.

## Unicode preparation

`U8G2_WITH_UNICODE` is enabled and MiaoUI `Disp_DrawStr` now forwards
UTF-8 to u8g2. A separate CJK glyph font is required before Chinese appears.
Current ASCII font has no Chinese; 4-byte Unicode beyond BMP is unsupported.

## Local CubeMX heap setting (not changed here)

The checked-in `.ioc` has `ProjectManager.HeapSize=0x000`.
Set **Minimum Heap Size = 0x1000 (4096 bytes)** in local CubeMX, regenerate,
build, and inspect RAM usage. The C runtime call chain `_init_alloc/_RT...`
does not by itself prove that the USBPD stack allocated the heap;
the USBPD minimal sink uses `USBPD_PE_GetMemoryConsumption()` to query
stack memory. Do not confuse FreeRTOS TOTAL_HEAP_SIZE with the C heap;
IH01A is non-RTOS. Stack stays at 0x400 pending a stack-usage check.
