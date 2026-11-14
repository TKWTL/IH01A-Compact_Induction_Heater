# IH01A USB-PD 实验记录

本目录存放 **USB-PD Sink 协议与供电链路的实测和故障分析**，不再保存迁移交接、待办计划或重复实现状态稿。

- [SPR / EPR 20 V → 28 V 验证](EPR_28V_VALIDATION.md)：FULL v5.4.1，28 V/5 A Fixed EPR 报文握手、KeepAlive 和约 6 小时稳定会话记录。
- [Lenovo C140 与 VBUS 重启故障](PD_C140_COMPARISON.md)：跨项目对比和 VBUS/NRST/CC 判别思路。
- [UART 诊断与复位取证](PD_UART_DIAG.md)：串口日志的实验解释。

**重要区分：** 28 V/5 A 是协议合同额度，不能证明 IH01A 功率级向锅具传送 140 W，也不能证明功率级达到了实用热功率。相关协议细节和源码见 [APP/algorithm](../../Firmware/APP/algorithm/README.md) 及 [Firmware/USBPD](../../Firmware/USBPD/)。
