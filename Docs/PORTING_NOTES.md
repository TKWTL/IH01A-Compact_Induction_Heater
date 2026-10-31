# IH01A 从 PB02B / DPC27A 移植记录

本次只建立基础框架，不迁入 DPC27A 的电源控制器驱动、PD 业务页面或 SH1107 后端。

| 原项目 | IH01A 处理 |
|---|---|
| DPC27A coroOS | 源码保留，TIME_Millis 映射 HAL_GetTick |
| DPC27A 定点 MiaoUI + 裁剪 u8g2 | 保留 MIT/BSD 授权，调整 128x64 和 SSD1315 驱动，移除 Dashboard 依赖 |
| DPC27A 双按键 | 全部重写为 L/R/ENTR/EXIT 低有效 4 键 |
| SH1107 80x128 R1/R3 | 改 SSD1315 128x64 R0/R2，SPI1 硬件 NSS |
| SH1107 外部 VPP 0xAD 0x8A | SSD1315 外部 VCC 0x8D 0x10；OLED RES# 默认接 STM32 PF2/NRST；NRST_MODE 保持 Reset input/output |
| PB02B PM | 经 DPC27A 无 RTOS 适配；保持 UI_OFF 限制，MCU PM 深睡钩子为空 |
| FreeRTOS | 不移植，使用 coroOS |
| DPC27A 端口算法 | 不移植，IH01A 业务算法尚未开始 |
| 中文 + 弹窗 | 本次保留原 MiaoUI 英文基础和原始参数弹窗，完整中文字体/通用提示弹窗尚未实现 |

重要风险：未在样机或 Keil ARM 编译器验证；DPC27A 当前同步分支也未经样机烧录。
此外 USBPD 需要被频繁调度，所以 LCD 初始化的供电等待放在协程，刷屏每轮限制128字节；
这只能降低阻塞，不构成最坏情况时序保证，必须用 PD 抓包验证。

根菜单仅 Main / Settings / Data / About 四项。Main 和 Data 暂为占位，Settings 保留 Sleep/Display 配置。当前字库只能显示英文标签，中文字体仍待移植。
