# IH01A · Heater UART CLI: 2S2P / CT 0.075V/A

USART1 PB6/PB7, 115200 8N1. Send lowercase ASCII + LF; each command returns one line prefixed by `HCLI OK` or `HCLI ERR`. PD diagnostic lines may interleave.

## Commands

| Command | Behavior |
|---|---|
| `heater phase 180` | Set 0..180° phase (default **180°**) |
| `heater freq 200` | Manual frequency, **150..400 kHz** (integer 1 kHz request), only while auto is OFF |
| `heater auto off` | `HCLI OK AUTO_FREQ=OFF` |
| `heater auto on` | **`HCLI ERR CT_ZCD_RECAL_REQUIRED`** until CT_ZERO has been recalibrated for 0.075V/A |
| `heater on` | One guarded PC6 enable request; no bypass of PD/NTC/Break/ADC interlocks |
| `heater off` | Pull PC6 LOW immediately |
| `heater get` | Read telemetry, including physical frequency/phase, CT current and raw/compensated IC2 status |

Malformed frequency input returns `HCLI ERR SYNTAX`; outside 150..400 returns `HCLI ERR RANGE`. If auto mode is ever revalidated and enabled, manual frequency writes return `HCLI ERR AUTO_FREQ_ENABLED`. Frequency writes continue using the existing atomic TIM1 ARR+CCR1/3/4 preload transaction without stopping the PWM carrier. Never infer zero-voltage switching (ZVS) from a valid register write.

## Settings → Heater

- `Phase deg`: defaults to **180°**, 10° UI increments.
- `Auto Freq`: OFF at power-up. The loop remains compiled but **enabling is refused** until new comparator timing measurements establish the CT_ZERO model.
- `Freq kHz`: editable with **10 kHz** steps when Auto Freq is OFF; endpoint wrapping is rejected. When Auto Freq is ON, edits are reverted.
- `CT_RMS` 显示零点状态：等待、均值采集、失败或有效电流，而不是无限保持 `calibrating`。
- `IBUS` 当前输入电流在 Main Heater 及 Settings/Heater 显示，沿用原 0.2134V/A 换算；两页均取消 CT0 数字行（Statistics/System 仅保留 IB0）。
- `LagC` 在160～330kHz旧模型有结果时直接显示估计角，不再以 `RECAL` 占位。自动追频资格判断仍要求新 CT 标定。


## 手动故障恢复（2026-11-05）

`heater on` 和 MiaoUI 的 PC6 ON / Heater RUN 均走相同的同步安全门控：

- 保护事件立即拉低 PC6、禁止 TIM1 MOE，并在故障弹窗保留最近的故障码；**不会自行重启**。
- 下一次人工 ON 才尝试确认先前的**瞬态故障**：重新检查最新的 12V/≤3A PD 合约、VBUS、零点有效性、IBUS 原始和滤波采样、NTC、COMP1/2/PA6 窗口以及至少约 100ms 的故障冷却。
- 校验通过后重新启用 ADC AWD1/AWD2/AWD3 的中断；如果之前 TIM1 Break 清除了 MOE，则在 PC6 保持低电平的前提下重新确认模拟窗、清除旧 BIF，并恢复 TIM1 Break 中断和逻辑 PWM，最后才允许再次使能 PC6。
- 仍在过流/过温、PD 合约不符、采样失效，或 COMP 初始化失败时，手动 ON 仍返回阻止原因；不把“人工确认”视为禁用硬件保护的许可。
- 如果启动零点采集失败（`ZERO FAILED`），首次手动 RUN 只会在 PC6/MOE 关闭时请求重新采集 256+2048 帧 ADC 样本（约 115ms）；期间绝不自动开启加热。界面显示 `REZERO - KEEP OFF`，采集成功后显示 `ZERO OK - PRESS RUN`。**用户再次手动 RUN** 且其余保护检查全部通过，才清除可恢复故障并尝试开启输出；采集再次失败则继续保持关闭。
- `0x00000400` 是 `CONV_PROT_AWD_IBUS`，即 IBUS 模拟看门狗 2 越界（不是 CT 槽路电流）；ADC 一次性告警经过确认恢复后会重新使能，下一次真实越界仍会触发。

Main Heater 页和 Settings → Heater 均显示 **IBUS（A）**：使用采样零点作差，标称灵敏度 0.2134V/A，单位显示至 0.01A。这是低通滤波诊断值，而非 AWD2 用于保护的原始瞬时采样。Statistics → System 不再列出独立的 IBUS 引脚电压行，仍保留启动零点及 AWD2 门限等故障诊断项。

## Measurement calibration

The proposed CT_OUT hardware gain changes **0.100 → 0.075 V/A**. Assuming the existing downstream full-wave rectifier and RC filter gains stay unchanged, nominal CT_RMS transfer is **0.25253 × 0.75 = 0.1893975 V/A**, relative to the startup zero. This is an estimated display conversion until measured.

At 2.5V ADC reference, ±12A peak current corresponds to a CT_OUT swing ±0.900V and ±1474 DAC counts, rather than ±1966. The default comparator OCP window is updated to **preserve ±12A**; allowing ±16A requires separate hardware validation. **IBUS remains 0.2134 V/A** and its existing ADC watchdog settings are unchanged.

Because comparator overdrive and input slew change, the historical CT_ZERO delay regression (0.100V/A) must not be used as valid phase compensation. Until recalibration:
- `LAG_RAW_DEG` and `IC2` report the uncorrected capture for data collection.
- Within an otherwise eligible capture, `LAG_DEG=NA` and `LAG_QUAL=RECAL`.
- Outside the existing phase, current or frequency gates, statuses such as `PHASE`, `LOW_CT` and `FREQ` still apply.
- The 5ms ADC DMA auto-frequency control loop stays disabled. Its internal 50mA nominal CT threshold is updated from 21 to 16 codes for future use, but **no new tracking calibration is asserted**.

## Telemetry and host client

Illustrative telemetry:
```text
HCLI OK HEATER=ON PHASE_DEG=180 FREQ_KHZ=200 FREQ_HZ=200000 IC2=444 LAG_DEG=NA LAG_RAW_DEG=+114.9 LAG_QUAL=RECAL CT_RMS_A=0.932 AUTO_FREQ=OFF AF_STATE=OFF IBUS_DELTA=0
```

Run:
```powershell
py Tools/heater_cli_client.py --port COM4 phase 180
py Tools/heater_cli_client.py --port COM4 auto off
py Tools/heater_cli_client.py --port COM4 freq 200
py Tools/heater_cli_client.py --port COM4 get
py Tools/heater_cli_client.py --port COM4 off
```

`heater freq` reports **realized rounded kHz**, as actual TIM1 timer ARR is integer. The manual frequency setpoint is not persisted over reset; boot baseline remains **300 kHz**. If connected to a load, test VDS/VGS and the tank current against the actual switching voltage phase, and qualify the modified winding's impedance, capacitor current, current transformer polarity, and hardware comparator trip at low voltage and limited supply current before increasing input power.

Run `bash Firmware/Tests/host/run.sh` for portable C99 tests. Keil target rebuild and hardware measurement are separately required.
