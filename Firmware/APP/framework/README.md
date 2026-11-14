# IH01A Framework

本目录为通用服务层，与特定功率控制算法分离。

| 文件 | 职责 |
|---|---|
| `system.c/.h` | 系统复位与系统菜单 |
| `time_service.c/.h` | RTC 相对开机时间与 Uptime |
| `beep.c/.h`、`beep_hw.c/.h` | 蜂鸣器控制和硬件驱动 |
| `pm_*.c/.h` | UI_OFF 级休眠策略与 MiaoUI 设置绑定 |

技术说明：[蜂鸣器](BEEP_FRAMEWORK.md) · [I/O 所有权与 coroOS 调度](CORE_IO_ARCHITECTURE.md) · [固件结构](../../ARCHITECTURE.md)。

本目录的文档侧重代码设计和接口，不重复存放在 `Docs/` 实验区。
