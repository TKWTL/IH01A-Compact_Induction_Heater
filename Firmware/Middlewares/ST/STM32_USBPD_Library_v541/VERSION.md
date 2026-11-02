# STM32 USB-PD middleware (FULL / EPR-capable) — vendored copy

本目录是 ST 官方 USB-PD 中间件的 vendored 拷贝。IH01A 的
`feature/pd-epr-28v5a` 分支已使用此处的 FULL CM0+ Keil 静态库和匹配头文件，
实测完成 20V SPR 与 28V EPR 协商。请勿与旧 MINSNK 头文件或二进制混用。
KeepAlive 内部调度仍由 ST FULL 库负责，当前未修改原始静态库。

## 来源与版本

| 组件 | 仓库 | 提交 | 日期 | 说明 |
|---|---|---|---|---|
| Core (inc + FULL lib + trace) | https://github.com/STMicroelectronics/stm32-mw-usbpd-core | `aafa359c9fe9edf2300f4261118488bad4e68d5e` | 2026-06-16 | Release v5.4.1 |
| Device layer STM32G0XX | https://github.com/STMicroelectronics/stm32-mw-usbpd-device-g0 | `527363f2a8fac11bd93507c568ef90abb0558bcb` | 2025-12-04 | [DOC] Update existing MD files and add SECURITY.md and issue templates |

许可: 见 `Core/LICENSE.md` 与 `Devices/STM32G0XX/LICENSE.md`(ST 授权条款,随 STM32 软件包)。

## 只取了需要的部分

- `Core/inc`: 10 个头文件(含新增的 `usbpd_pe_epr.h` / `usbpd_pe_extctrl.h` / `usbpd_pe_genericmachine.h` / `usbpd_tcpm.h`)
- `Core/lib/USBPDCORE_PD3_FULL_CM0PLUS_Keil.lib`: 仅取 CM0PLUS + Keil 这一种(其它架构/变体未取)
- `Core/src/usbpd_trace.c`: 与 FULL 栈配套的 trace 实现
- `Devices/STM32G0XX`: G0 设备层(7 个 .c + 6 个 .h + 新的 `usbpd_devices_conf_template.h`)

未取: MINSNK/SPR/MINSRC 等其它 lib 变体、`usbpd_dpm_core.c`/`usbpd_usb_if_template.c`(工程侧另有一份 `Firmware/USBPD/App/usbpd_dpm_core.c`,迁移时需与新版对照)。

## 注意

- 这是**二进制 + 头文件**匹配的一套,不要和旧目录的头文件混用(结构体/枚举位置不同)。
- 切换步骤、需要新增的 `usbpd_devices_conf.h`、以及 `.uvprojx` 修改清单见 `Docs/USBPD_FULL_EPR_MIGRATION_IMPACT.md`。