# IH01A 基础移植测试记录

此阶段没有连接实机或执行 Keil 完整编译。以下项目均为 **待验证**，不能标为通过。

| 验证项 | 期望行为 | 状态 |
|---|---|---|
| 工程编译 | ARMClang/Keil无错误，Flash<128KiB、RAM<36KiB | 待编译 |
| OLED RST 共网 | PF2/NRST 连 SSD1315 RES#，默认 NRST_MODE；内外复位均触发屏幕复位 | 待测 |
| OLED 外部VCC | 外部 VCC 稳定时序及共网 RES# 复位低脉冲满足 SSD1315 要求 | 待测 |
| 电荷泵 | SSD1315发送8D 10、绝不发送8D 14 | 源码静态检查通过，待抓SPI |
| SPI OLED | PA15/PB3/PB5/PB4；128×64 正常画面 | 待测 |
| 四键低电平 | L上、R下、ENTR确认、EXIT退出 | 待测 |
| UI_OFF | 关屏但 USBPD、风扇/保护机制不中断 | 待测 |
| USBPD | 插拔、协商、通信负载下稳定 | 待测 |
| PWM 安全 | 首版所有操作都不使能加热PWM | 待测 |

主机侧 PM 计时器回归测试参见 `Firmware/Tests/host`；不能替代目标 MCU 集成编译和实机验证。

根菜单四项的静态源码检查完成，Main/Data 不展示未经验证的传感器值；OLED RST 实际布线和电平仍待上板核验。

## 已报告的 Keil 链接错误（本次修正）

构建者报告 `L6218E: Undefined symbol u8x8_capture_*` 共六项，调用点位于 `u8g2_buffer.o`。这些符号属于未包含的 PBM/XBM 捕获导出实现，不影响当前 SSD1315 常规渲染。已改为仅在显式启用 `U8G2_ENABLE_BUFFER_CAPTURE` 时编译导出函数，并将 coroOS 文件移动至 `MDK-ARM/Scheduler`、更新 `uvprojx`。

**修复后的完整 Keil 链接仍待用户本地重建确认**；此条记录不表示编译或上板验证已通过。

## Keil 调试阻塞于 _ttywrch（用户提供反汇编）

报告位置约 `0x08000CEA: BKPT 0xAB`，属于 ARM semihosting；
由于未获得 Call Stack，无法断定是 C 库启动、输出、断言还是退出报告。
当前工程使用 ARMCLANG 6.24 标准库，无参数 `main()`。
修订措施：`MDK-ARM/retarget.c` 保证无命令行参数启动，不使用 semihosting，
重定向 `_ttywrch` / `fputc` 到 128B 调试环，`_sys_exit` 保留可观察退出状态；
`APP/framework` 拆分为独立 Keil 编译组。修复后尚需实机断点核对及 Rebuild。

**若链接出现 L6915E**：表示还有其他没有被重定向的 C 库 semihosting
函数，不应删除 `__use_no_semihosting` 绕过；应依据报错逐一处理。
