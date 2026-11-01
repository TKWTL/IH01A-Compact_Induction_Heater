# RAM 执行速度优化实践（参考样例）

> ⚠️ 本目录下的 `stm32g4xx_it.c` 与 `stm32g474xx_flash.sct` **不属于 PowerControl**。
> 中断向量表归 `Core/Src/` 管，链接分区归 `MDK-ARM/` 管；它们被临时放在这里，只为给移植者一份
> "能照着抄"的样例。**移植完成后按文末清单删除（卸磨杀驴）。**

## 1. 源工程为什么要这么做

- 源工程是 STM32G474，主频 120 MHz **超频到 240 MHz**，Flash 读等待被设成
  `LL_FLASH_SetLatency(LL_FLASH_LATENCY_5)`（5 个等待周期）。
- 控制环 `MainLoop()` 由 **ADC-DMA 半传输中断**触发，属于开关频率级中断，每个 PWM 周期必须跑完。
- 代码若留在 Flash，取指要等 5 WS：ISR 入口延迟 + 环路计算抖动会直接变成占空比更新延迟，
  环路相位裕度和输出 THD 都会变差。
- **CCM SRAM**（`0x10000000`，源片约 32 KB）零等待、总线独立：CPU 在 CCM 取指/取数时，
  DMA 仍可满速访问 SRAM1/2，互不抢带宽。于是把"每周期都跑"的代码搬进 CCM。

## 2. 三处具体措施（与两个参考文件一一对应）

| # | 措施 | 落点 | 关键点 |
|---|---|---|---|
| ① | 热路径函数进 CCM | `main.h`: `#define RAMFUNC __attribute__((section(".ccm_text"), used))`<br>`stm32g4xx_it.c`: `RAMFUNC void DMA2_Channel1_IRQHandler(void){ MainLoop(); }` | 只标真正每周期执行的函数（本例是 ADC-DMA 半传输 → `MainLoop`，TC 中断已关闭）；`used` 防止链接器把"没人直接调用"的函数删掉 |
| ② | 中断向量表进 CCM | `.sct`: `ER_VECTORS 0x10007E00 EMPTY 0x00000200`<br>`main.c`: 上电把 Flash 里 512 B 向量表拷到该地址，再 `__DSB(); SCB->VTOR = VECT_TAB_START; __DSB(); __ISB();` | 中断入口少一次 Flash 取指；拷贝期间 `__disable_irq()`；源地址用链接器符号 `Load$$ER_IROM1$$Base`，不硬编码 |
| ③ | 快速变量 / 常量池进 SRAM2 | `main.h`: `RAMVAR` → `.ram_data`、`RAMRO` → `.ram_ro`<br>`.sct`: `RW_SRAM2 0x20014000 0x0003000 { *(.ram_ro) *(.ram_data) }`，栈 4 KB 置顶 | 环路系数、正弦表、滤波状态放这里；跨中断共享的变量仍要 `volatile` + 临界区 |
| ④ | 其余变量走默认区 | `.sct`: `RW_SRAM1 0x20000000 0x00014000 { .ANY (+RW +ZI) }`、`RW_MSP_STACK 0x20018000 -0x00001000 { *(STACK) }` | CCM / SRAM2 容量都很小，撑爆会直接链接失败，优先级必须按"每周期执行"来排 |

## 3. 代价与坑

- CCM 给代码只留了 30 KB、SRAM2 只留 12 KB；**优化是有预算的**，不是越多越好。
- 用了 `section` 属性后，函数实际地址在 RAM：看反汇编/断点要对 `--scatter` 过的 `.map`，
  Keil 的 Target 选项里必须挂上改过的 `.sct`，否则改动静默失效。
- 向量表必须**拷完再开中断**，`VTOR` 更新前后都要屏障（`DSB`/`ISB`）。
- 上电/复位后要靠启动流程重新拷贝，不要假设 RAM 内容还在（本例放在 `main()` 早期）。
- 附录里的 `#if 0` 块是留档：早期用 `hdma_adc4` + TC 判断触发 `MainLoop`，后来改成半传输触发，
  旧写法保留供对比，**不要把 `#if 0` 里的代码当真**。

## 4. 移植到本工程（STM32G071CBUx / Cortex-M0+）

| 事实 | 影响 |
|---|---|
| G0 **没有 CCM SRAM**（36 KB SRAM，统一总线） | 没有零等待专用区，只能"Flash → SRAM 自拷贝"或直接跑在 SRAM |
| 主频 64 MHz、Flash 等待周期少（1~2 WS） | 相对收益远小于源工程 240 MHz / 5 WS，**优先级低，先别做** |
| M0+ 无 I/D cache，取指靠 Flash 预取 | RAM 执行仍有收益，但边际很小 |
| 本工程中断在 `Core/Src/stm32g0xx_it.c` | `RAMFUNC` 要加在 `TIM1_*` / `DMA_*` 的 Handler 上，不是 G4 的 `HRTIM1_FLT_IRQHandler` |
| 本工程链接脚本在 `MDK-ARM/Ld/Compact_Induction_Heater.sct` | 要加 RAM 区就改这一份，别拷 g474 的那份 |

若将来真做，最小改动是：

1. `#define RAMFUNC __attribute__((section(".ramfunc"), used))`，标在热路径 ISR + 控制环函数上；
2. `.sct` 增加一个 `ER_RAMTEXT` 区（SRAM 内），ROM 侧保留 `+RO` 副本，启动时做一次 RAM 拷贝；
3. 向量表副本放 SRAM，`SCB->VTOR` 指过去（G0 同样支持 VTOR）；
4. 用示波器量"ISR 入口 → 占空比寄存器写入"的延迟差，确认收益再继续。

## 5. 卸磨杀驴清单（移植完成后删除）

```
Firmware/APP/PowerControl/stm32g4xx_it.c          ← 只留 Core/Src/stm32g0xx_it.c 中的 RAMFUNC 标注
Firmware/APP/PowerControl/stm32g474xx_flash.sct   ← 只留 MDK-ARM/Ld/*.sct 中的分区改动
Firmware/APP/PowerControl/README_RAM_EXEC.md      ← 本文件
```

删除命令：

```bash
git rm Firmware/APP/PowerControl/stm32g4xx_it.c \
       Firmware/APP/PowerControl/stm32g474xx_flash.sct \
       Firmware/APP/PowerControl/README_RAM_EXEC.md
```

**判断标准**：`stm32g0xx_it.c` 里已出现 `RAMFUNC` 标注、`.sct` 里已有 RAM 执行区、且测到中断延迟确有改善
——就说明这套样例已经被"消化"，三个文件可以删；删掉后 PowerControl 目录只剩纯算法/控制源码。

## 6. 出处

- 源工程 NUEDC_2026A（2026 电赛 A 题）：
  `Firmware_0/Core/Src/stm32g4xx_it.c`、`Firmware_0/MDK-ARM/stm32g474xx_flash.sct`、
  `Firmware_0/Core/Inc/main.h`（`RAMFUNC` / `RAMVAR` / `RAMRO`）、`Firmware_0/Core/Src/main.c`（`RelocateVectorTable()`）
- 源工程说明：`NUEDC_2026A/README.md` 的「硬件平台 → CCM SRAM」一节
