# IH01A 实验结果索引

> **研究性资料，不是安全、效率或可用功率认证。** 实验条件随硬件版本（CT 增益、绕组接法、输入供电）变化，应阅读各报告的日期、限制与原始记录。

1. [12 V 空载 450 点扫频扫相](SWEEP_12V_UNLOADED_REPORT.md)：160～400 kHz、10～180°，含 CSV 记录；空载最大 CT_RMS 1.687 A。
2. [低功率手动加热与零电流采集](LOW_POWER_ZCD_BENCH.md)：捕获链路和安全门控。
3. [ZCD 相位补偿](ZCD_PHASE_COMPENSATION.md)：历史拟合，不得直接用于改绕组后的 ZVS 确认。
4. [双周期检锅](PAN_DETECTION_PULSE_RINGDOWN_STUDY_2026-11-10.md)：六组 CCR2 捕获、四组波形；目前不足以建立鲁棒的检锅判断。
5. [过流与过温保护接入](POWERCONTROL_PROTECTION_BRINGUP.md)：硬件/固件结构与验证要求。
6. [手动控制与实验设置](../../Firmware/APP/PowerControl/HEATER_SETTINGS_BENCH.md)：测试参数、CT_RMS 与接口限制。

示波器与 PCB 图片由 [Images 目录](../../Images/README.md) 索引。PD 实验另见 [Docs/PD](../PD/README.md)。
