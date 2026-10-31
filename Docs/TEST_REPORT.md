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
