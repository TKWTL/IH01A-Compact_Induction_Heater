# IH01A framework/beep 使用说明

- 定时器：TIM14 CH1 / PF0（`main.h` 的 `BEEP`），GPIO AF2 已由 CubeMX 配置，独立于 USBPD/主加热 PWM。
- 当前主频 64 MHz，APB1=32 MHz、TIM14=64 MHz。PSC=63，计数周期为 1 µs，因此频率由 `f ≈ 1000000/(ARR+1)` 决定；CCR=(ARR+1)/2，约 50% 占空比。
- 音符结构 `{arr, duration_ms}`。**arr=0** 是静音音符，可持续一段时间；**duration_ms=0** 为序列结束。用 `BEEP_END` 放在最后。
- 序列编写时用 `BEEP_C4`、`BEEP_A4`、`BEEP_C5` 等音名宏，或用 `BEEP_ARR_HZ(2700u)` 编译期转换。不要向驱动传递 Hz。
- 调度器新增 `thread_beep`，一次轮询只处理一个音符切换，不阻塞，不使用定时器中断。队列容量 8，保存 `const beep_note_t *`，必须使用静态或持久存储；不得传入局部栈数组。
- `beep_enqueue(sequence)` 返回 1 成功、0 失败；`beep_stop()` 清空队列；`beep_note_index()` 查询当前序列下标。序列超过 64 项会安全结束。无抢占播放，FIFO 顺序。
- `beep_set_key_enabled(0/1)` 控制 100 ms、约 2.7 kHz 按键反馈。默认关闭；设置菜单的 `Click Beep` 控制它。按键音效在 `thread_input_pm` 内与 `pm_api_refresh_idle()` 同时触发；关屏唤醒键不触发音效。
- `Mute Beep` 是默认关闭的独立全局静音开关。开启后立即停止当前音序并清除队列；静音期间任何来源的播放请求都被拒绝，不改变 `Click Beep`。
- `Play Music` 将 `Mute Beep` 清零（菜单开关同步更新），然后排队播放帕赫贝尔《D大调卡农》第一小提琴声部初次进入的前两个小节（乐曲原版第3–4小节）。旋律 F#5 E5 D5 C#5 / B4 A4 B4 C#5，使用缩短节奏的单声部铃声版本。原作品为公有领域。
- `System Reset` 始终保持 Settings → System 菜单最后一项。
- 弹窗先 `UI_Dialog_SetAudio(ui, melody)`，随后 `Dialog_Show()` / `Notifications()` 的绘制阶段自动入队一次；离开弹窗时清除指针。将来添加事件队列时把音频指针复制到弹窗事件即可。
- 误差：低频音符由于定时器整数计数与音名宏的整数 Hz 四舍五入产生微小频率偏差；若改变时钟树，应同时更新 `beep_hw_init()` 的预分频器与 `BEEP_TICK_HZ`，否则音高会变化。
- 本提交不触碰已验证的 EPR Request、Native KeepAlive 和 5V/28V 路径。

示例：

```c
static const beep_note_t melody[] = {
    {BEEP_C5, 120u},
    {BEEP_REST, 60u},
    {BEEP_E5, 160u},
    BEEP_END
};
(void)beep_enqueue(melody);
```

在仓库根目录的可用 Shell 环境运行 `sh Firmware/Tests/host/run.sh`。
Keil 必须 Rebuild，再检查编译结果与 TIM14/PF0 输出。当前无实机验证。
