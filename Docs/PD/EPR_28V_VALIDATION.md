# IH01A PD3.1 EPR 28 V/5 A 验证摘录

来源：2026-11-02 ST FULL v5.4.1 板端串口记录及 PulseView 协议抓包（原始内部迁移状态稿经整理，仅保留实验证据与条件）。不将协商电流等同于实际负载电流。

## 20 V SPR 基线

```text
[PD_INIT] LIB_ID=0x30410000 check=1 reqmem=4506 heap=8192
[PD_INIT] FULL v5.4.1 initialized
[PD 594] SPR_CAPS a=6 b=0x0A81912C
[PD 597] REQUEST a=20000 b=0x5104B12C
[PD 859] CONTRACT a=1 b=0x00000010
[CLI] bus=20048 mV select=20000 mV current=3000 mA PDO=5
```

这份记录同时包含 SPR 合同与 VBUS ADC 值，表明在该次测试中 20 V 选择与采样一致。测试阶段出现过需断电恢复的硬复位，不能理解为全部插拔压力测试通过。

## 首次 28 V EPR 成功会话

```text
EPR_REQUEST a=28000 b=0x81C7D1F4
PD_TXDO do1=0x81C7D1F4 do2=0x0008C1F4
PD_RX a=3 ...                 # Source Accept
CONTRACT a=1 ...              # PS_RDY / EPR Fixed 合同
```

- EPR 对象位置使用绝对位置 **8..13**；发送 RDO 时必须同时正确提供 CopyPDO。
- RDO 的 EPR Capable、Unchunked Extended Messages 与 No USB Suspend 位需要与栈的要求一致；曾因缺位导致硬复位。
- 该板使用外部 CC Rd，ST 栈的 SinkTxOk 判定曾导致主动消息静默丢弃；本次使用兼容修正通过验证。不能仅凭这次修正推断所有 Type-C/PD 合规条件已满足。
- 当时实测 EPR KeepAlive 间隔约 **500 ms**，连续 **6 小时**未断联。随后将应用侧补发间隔调整为 **375 ms**；新策略下是否与栈自带 KeepAlive 重叠，仍需单独抓包。
- 该摘录支持 **协议状态机已进入 28 V Fixed EPR 合同**，但不包含该次 28 V 会话的独立示波器 VBUS 波形、输入电流和实际加热功率测量。

## 适用边界

高压测试必须核实电源、线缆 e-marker、输入保护、板载器件耐压和硬件联锁。一次协商成功不是长期产品稳定性证据。本项目最终定位仍为感应加热技术储备，不将 EPR 功能可用等同于实际加热能力达标。
