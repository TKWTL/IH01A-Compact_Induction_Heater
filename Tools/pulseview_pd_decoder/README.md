# PulseView `usb_power_delivery` 解码器：PD 3.1 / 3.2（EPR）增强

本目录保存本机 PulseView 使用的 `usb_power_delivery` 解码器补丁版本，以及可离线运行的
回归测试。用途：在逻辑分析仪抓取 IH01A 的 PD 会话时，能正确显示 EPR 相关报文与对象，
便于定位"源端不支持 / 进入失败 / 对象选错 / 协商后失败"这几类问题。

## 安装位置

```
D:\Program Files (x86)\sigrok\PulseView\share\libsigrokdecode\decoders\usb_power_delivery\pd.py
```

原版备份在同目录的 `pd.py.bak`（20708 B，2025-11-10）。

部署（PowerShell）：

```powershell
$dst = 'D:\Program Files (x86)\sigrok\PulseView\share\libsigrokdecode\decoders\usb_power_delivery'
Copy-Item 'Tools\pulseview_pd_decoder\pd.py' "$dst\pd.py" -Force
Remove-Item "$dst\__pycache__" -Recurse -Force -ErrorAction SilentlyContinue   # 强制重新编译
```

回滚：

```powershell
Copy-Item "$dst\pd.py.bak" "$dst\pd.py" -Force
```

改完必须**重启 PulseView**（解码器在启动时加载，热替换无效）。

## 相比原版增加的内容

| 项 | 说明 |
|---|---|
| `DATA_TYPES` | 补 `9 = EPR Request`、`10 = EPR Mode`（原版显示 `DAT???`） |
| `EXT_TYPES` | PD3.0/3.1/3.2 扩展报文类型表（`0x01 Source_Capabilities_Extended` … `0x11 EPR_Source_Capabilities`、`0x12 EPR_Sink_Capabilities`、`0x10 Extended_Control`、`0x1E Vendor_Defined_Extended`） |
| `EXT_CTRL_TYPES` | `Extended_Control` 的控制类型：1 `EPR_Get_Source_Cap`、2 `EPR_Get_Sink_Cap`、3 `EPR_KeepAlive`、4 `EPR_KeepAlive_Ack` |
| 扩展报文分帧 | 解析扩展头（`chunked` / `chunk number` / `chunk request` / `data size`），支持 **分块重组** 与 **非分块** 两种传输；`Chunk Request` 单独标注 |
| CRC | 扩展报文的 CRC 覆盖 `header + extended header + 线上全部字节（含填充）`；非扩展报文维持原实现。**实测 77/77 帧全部通过** |
| APDO | 区分 `PPS(subtype 0)`、`EPR AVS(subtype 1)`、`SPR AVS(subtype 2)`，输出 `min/max V`、`PDP W`、`9-15V/15-20V` 电流 |
| RDO | 对象位置改为 4 bit（支持 `8..13` 的 EPR 对象）；区分 PPS RDO（20 mV）与 AVS RDO（25 mV、50 mA、7 bit 电流，步进 100 mV） |
| `EPR_Mode` | 输出 `action`（Enter / Enter ACK / Enter Succeeded / Enter Failed / Exit）与 `sink PDP` |
| `EPR_Request` | 两个数据对象分别解码：DO0 = RDO，DO1 = 被请求 PDO 的副本（原版按单对象 Request 处理） |
| 源端 Fixed PDO | 增加 `bit23 = epr_capable`（PD3.1 的 EPR 支持位，正是我们判断源端能否进 EPR 的依据） |
| `cap_mark`/`stored_pdos` | 容量扩到 16 个对象，供 8..13 号 EPR 对象引用 |

## 回归测试（不依赖 PulseView）

```powershell
python Tools\pulseview_pd_decoder\test_decoder.py
```

测试向量全部来自真实抓包（`Desktop\pd decode.txt`），覆盖：

- `0x0A81912C` → `[Fixed] 5V 3A [epr_capable]`（源端 PDO1，EPR 能力位）
- `0x0008C1F4` → `[Fixed] 28V 5A (140W)`（EPR 固定档）
- `0xD230968C` → `[EPR AVS] 15/28V PDP 140W`
- `0x51C7D1F4` → `(PDO #5) 5A (operating) / 5A (max) [no_suspend] [unchunked]`
- `0x81C7D1F4` → `(PDO #8: Fixed 28V) …`（EPR 请求，对象位置 8）
- `0x8008C064` → `EPR AVS 28V 5A`（AVS RDO 示例）
- EPR 能力的两段分块（26 B + 10 B，`data size = 36`）重组后得到 9 个对象：
  `5V/9V/12V/15V/20V/PPS/空/28V/EPR AVS`

## 已知限制

- PulseView 自带 Python 3.4 运行库，**不能用 f-string / walrus / 3.5+ 语法**；本文件已按 3.4 语法写（`test_decoder.py` 会用 3.9 做语法与 `py_compile` 检查，并扫描 f-string 等不兼容构造）。
- 只增加"识别与解析"，没有增加协议状态机推断（例如自动判断"该 Source 未提供 EPR 能力"）。
- 安全/固件升级类扩展报文只显示类型名，不解析内容。
- 分块只做"按 chunk number 顺序拼接"，不做主动重传请求；乱序/丢块会显示 `partial`。
