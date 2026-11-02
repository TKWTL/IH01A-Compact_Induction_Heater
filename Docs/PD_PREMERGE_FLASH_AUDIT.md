# IH01A EPR 分支合并前：最小化 PD 日志与 Flash 链接审查

## 范围和不变量

基于 `feature/pd-epr-28v5a@33ee6e4` 的已验证 EPR Mode 内降压行为。
**禁止**把 `PD_SendPolicyRequestDirect()` 改回 EPR_Mode Exit；保留
`USBPD_PE_Send_Request()` 生成 RDO + 源 PDO 副本的 EPR_Request 及 ST FULL 自主管理的
约 500 ms KeepAlive。没有更改 ST 静态库、选档策略、PHY CC 发送或 MCU 复位处理。

生产 UART 只主动输出四类 PD 日志：

- Source PDO：收到时以整数电压、电流、PPS / EPR AVS 可读方式输出，按对象位置和数据去重；共用 `PD_SourceStats_FormatPDO()`，UI 与 UART 不重复实现解码；
- RDO：含对象位置、请求电压、电流，标识 EPR_Request 形式（在 EPR Mode 内请求 SPR PDO 时亦适用）；
- Hard Reset：打印 ST `USBPD_HR_Status_TypeDef` 状态文字；
- API 回显：开关 Fixed 请求、修改最大 Fixed 电压；`pd max` 在 OFF 时只保存，仍保持原 CLI 三态语义。

删除默认收发原始十六进制帧、GoodCRC、探针、GETINFO/SETINFO、通知流水账；
`PD_Diag_Record` 的编译期筛选使被压制的调用及其参数计算从 ARMCLANG
生成代码中删除。初始 UART `U1OK` 探针、异常初始化失败信息、HardFault
及 Fatal 信息保留，避免取消现场故障定位路径。

## 用户上传的 armlink 6.24 map 基线

上传文件为 `Compact_Induction_Heater.map`；不假定它与 Git HEAD 完全相同。
必须重新 Rebuild 才能得出新固件真实大小。

| 项目 | 基线 |
|---|---:|
| Code + RO | 88,876 B |
| RW initialization in Flash | 476 B |
| **Total ROM Size** | **89,352 B (87.26 KiB)** |
| RW + ZI (RAM) | 23,928 B |
| STM32G071CBUx 名义 Flash | 131,072 B |
| 名义未使用 Flash | 41,720 B |
| FULL 静态库链接成员，代码 + RO + RW | 28,020 B |
| `pd_input.o` 代码 | 5,462 B |
| `ui.o` 代码 | 3,956 B |
| `console_cli.o` 代码 + RO + RW | 1,670 B |
| `pd_diag.o` 代码 + RO + RW | 1,217 B |
| `pd_diag.o` ZI | 2,075 B |

map 的 Debug column **不计入 Flash**；原始 .lib 文件大小不等于被烧录的大小。
堆 8KiB 及 ST FULL 依赖当前保留，勿因减少 UART 日志而直接删除堆或降低核心
USBPD 功能。HAL、u8g2 及 UI 是现用功能，暂不做破坏性裁剪。

## 低风险优化及预算

1. `pd_diag.c` 环形诊断队列从 128 × 16 B 减到 32 × 16 B，
   **队列本身预留约少 1,536 B RAM**；新增少量 PDO 去重状态使实际净节省
   不等于 1,536 B。Flush 每轮最多 16 条，避免占用太长 PE 主循环时间。
2. 共享 PDO 解码而非单独实现 UART 解析器；删除原始 PHY 事件的默认输出、
   成功 USBPD 启动时的详细打印及 UART 描述 banner。
3. 依靠编译期诊断筛选去掉诊断参数打包代码；**实际 Flash 下降需要同配置 Keil
   Rebuild + 新旧 map 数字比较**。不能把减少的源文件字符数当作 Flash 节省。
4. 不改 Keil 优化等级、不替换 printf 运行库、不裁撤正在使用的 U8g2 / HAL，
   不改 ST FULL 静态库。体积更大的核心模块涉及协议时序，单独评估。

## 本机验收

```powershell
# 从仓库根目录
& 'D:\Program Files\Keil_v5\UV4\UV4.exe' -r Firmware\MDK-ARM\Compact_Induction_Heater.uvprojx -j0 -o build.log

# 环境支持 sh、cc 时运行
sh Firmware/Tests/host/run.sh
python Tools/pulseview_pd_decoder/test_decoder.py
```

实机验证的最小矩阵：

| 输入 / 操作 | 预期 |
|---|---|
| 只支持 SPR 的电源插入 | 输出收到的 SPR PDO、最高合法 Fixed RDO，合同正确 |
| EPR 电源冷启动 | RDO 28V、合同 28V/5A、原生 KeepAlive 不变 |
| EPR 模式中执行 `pd off` | **EPR_Request 位置 1 + 对应 5V PDO 副本**；保持 EPR 会话 |
| 执行 `pd on` | EPR_Request 返回 28V；没有 EPR Exit |
| `pd max 15`、`pd max 20` | 请求位置 4、5，源 PDO 副本正确 |
| `pd off` 后 `pd max 15` | 只保存限制，`pd on` 后生效 |
| SW3538 20V/7A | RDO 为真实 Source 7000 mA，返回合同电流一致 |
| 主动或被动 Hard Reset | 显示可读 Status，不进入无限循环 |
| 软件计时 / 重负载 | 确认无串口丢帧及 PD 协议超时 |

当前只进行了源码静态审查，**没有在此环境完成 Keil 或板端回归**。
在新 map、`0 Error(s), 0 Warning(s)` 与上述关键行为核对通过前，不建议并入 main。
