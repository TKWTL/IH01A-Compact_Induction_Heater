# main.h / main.c 相关定义摘录（仅参考）

> ⚠️ **仅作参考，不属于 PowerControl。** 源的 `main.h` / `main.c` 是 CubeMX 生成的 G4 工程主文件，
> 整份拷进来 80% 是无关的外设初始化（HRTIM/ADC/COMP/DAC/FDCAN/FREERTOS…），对本工程没有复用价值。
> 这里只摘出**与 RAM 执行优化、以及 PowerControl 移植直接相关**的那几段，并标注原件行号，方便对照。
> 移植消化后按文末清单删除本文件（卸磨杀驴）。

## 1. main.h 摘录

### 1.1 HRTIM 周期与死区（`Core/Inc/main.h` 93–95）

```c
/* Private defines -----------------------------------------------------------*/
#define INITIAL_HRTIM_DEADTIME 240
#define INITIAL_HRTIM_PERIOD 48000
#define REC_HRTIM_DEADTIME 180
```

> 对应 `conv_pwm.c` 里整流/逆变两组半桥的初始占空比分辨率与死区计数值。
> 换算到本工程的 TIM1：`ARR` ≈ 计数时钟 / f_sw，死区写 `BDTR.DTG`（G0 的 DTG 编码与 G4 不同，需重算）。

### 1.2 RAM 区段宏 + 常量池声明（`Core/Inc/main.h` 121–151）

```c
/* USER CODE BEGIN Private defines */
//用于链接地址分配
#define RAMFUNC __attribute__((section(".ccm_text"), used))     //高频执行的程序，放在CCM SRAM
#define RAMVAR  __attribute__((section(".ram_data"), used))     //快速变量放在SRAM2
#define RAMRO   __attribute__((section(".ram_ro"), used))       //常量池也放在SRAM2

//魔数，用于校验
#define MAGIC 0x01919810U

//ADC参考电压值
#define ADC_VREF    3.300f

//SRAM2常量池
extern const float INV_3_F32;
extern const float INV_5_F32;
extern const float INV_6_F32;

extern const float SQRT2_F32;
extern const float SQRT3_F32;
extern const float SQRT6_F32;

extern const float INV_SQRT2_F32;
extern const float INV_SQRT3_F32;
extern const float INV_SQRT6_F32;

extern const float PI_F32;
extern const float PI2_F32;

extern const float INV_PI_F32;
extern const float INV_PI2_F32;
/* USER CODE END Private defines */
```

> 三个 `__attribute__((section(...)))` 宏是 `.sct` 里的 `ER_CCM_TEXT` / `RW_SRAM2` 能生效的前提，
> 见 `README_RAM_EXEC.md` 第 2 节。`ADC_VREF` 和常量池是 PowerControl 里 ADC 换算与三角函数运算的依赖。

## 2. main.c 摘录

### 2.1 SRAM2 常量池定义（`Core/Src/main.c` 60–79）

```c
/* USER CODE BEGIN PV */
//SRAM2常量池
RAMRO const float INV_3_F32 = 0.3333333433f;  // 1/3
RAMRO const float INV_5_F32 = 0.2000000000f;  // 1/5
RAMRO const float INV_6_F32 = 0.1666666716f;  // 1/6

RAMRO const float SQRT2_F32     = 1.41421356237f;
RAMRO const float SQRT3_F32     = 1.73205080757f;
RAMRO const float SQRT6_F32     = 2.44948974278f;

RAMRO const float INV_SQRT2_F32 = 0.70710678118f;
RAMRO const float INV_SQRT3_F32 = 0.57735026919f;
RAMRO const float INV_SQRT6_F32 = 0.40824829046f;

RAMRO const float PI_F32      = 3.14159265359f;
RAMRO const float PI2_F32     = 6.28318530718f;

RAMRO const float INV_PI_F32      = 0.31830988618f;  // 1/pi
RAMRO const float INV_PI2_F32     = 0.15915494309f;  // 1/(2*pi)
/* USER CODE END PV */
```

> 这些是 `conv_loop.c` / `algorithm_control.c` 里被反复读取的常数。放 SRAM2 后取用不再等 Flash；
> 代价是常量池会常驻 RAM（`.sct` 里 `RW_SRAM2` 只给了 12 KB）。
> 本工程若不做 RAM 优化，可以照抄定义但去掉 `RAMRO` 宏。

### 2.2 向量表重定位（`Core/Src/main.c` 106–126）

```c
/***********************移动中断向量表到SRAM以提升速度*************************/
#define VECT_TAB_START  0x10007E00
#define VECT_TAB_SIZE   0x200U
extern uint32_t Load$$ER_IROM1$$Base; // 获取 Flash 中原始向量表地址

void RelocateVectorTable(void) {
    const uint32_t *pSrc  = &Load$$ER_IROM1$$Base;  // Flash 中的向量表起始
    uint32_t *pDest = (uint32_t *)VECT_TAB_START;   // CCM RAM 中的目标位置

    __disable_irq();
    /* 1. 将向量表从 Flash 拷贝到 CCM SRAM */
    for (int i = 0; i < VECT_TAB_SIZE / 4; i++) {
        pDest[i] = pSrc[i];
    }
    /* 2. 更新 VTOR 寄存器，告知 CPU 新的地址 */
    __DSB(); // 数据同步屏障
    SCB->VTOR = VECT_TAB_START;
    __DSB();
    __ISB();
    __enable_irq();
}
```

> 与本目录的 `stm32g474xx_flash.sct`（`ER_VECTORS 0x10007E00 EMPTY 0x200`）配对使用。
> 注意 `Load$$ER_IROM1$$Base` 是 Keil 链接器符号；`VECT_TAB_START` 是**源片的 CCM 地址**，
> 本工程 G071 没有 CCM，要改成 SRAM 内的地址（并同样 256 字节对齐）。

### 2.3 超频到 240 MHz（`Core/Src/main.c` 128–159，**本工程不适用**）

```c
/**************************从120MHz超频至240MHz********************************/
//不影响外设时钟
void STM32G474_OverClock(void)
{
    ...
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);   // ← 5 个等待周期，正是要"把代码搬进 RAM"的原因
    ...
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_1, 40, LL_RCC_PLLR_DIV_2);
    ...
    LL_SetSystemCoreClock(240000000);
    if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK) Error_Handler();
}
```

> 只作为"为什么要做 RAM 执行"的背景证据：240 MHz + 5 WS 时，Flash 取指严重拖慢热路径。
> 本工程 G071 最高 64 MHz、Flash 等待 1~2 WS，**不需要也不应该抄这段**。

### 2.4 main() 启动顺序里与本主题相关的三行（`Core/Src/main.c` 188–197）

```c
  /* USER CODE BEGIN SysInit */
    //配置错误中断重定向
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;
    SCB->SHCSR |= SCB_SHCSR_BUSFAULTENA_Msk;
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk;
    //重定向中断向量表
    RelocateVectorTable();
    //手动超频到240MHz
    //STM32G474_OverClock();
  /* USER CODE END SysInit */
```

> 顺序很关键：**先开 fault、再搬向量表、最后才初始化外设并使能中断**。
> `MX_*_Init()` 之后才 `osKernelStart()`，保证 ISR 不会在向量表就位前触发。

## 3. 移植映射表

| 源（G474） | 本工程（G071）应放在哪 | 处理方式 |
|---|---|---|
| `RAMFUNC` / `RAMVAR` / `RAMRO` | `Firmware/Core/Inc/main.h`（或新建 `app_config.h`） | 宏保留，`section` 名按本工程 `.sct` 改（G0 无 CCM，用 `".ramfunc"` 之类） |
| `VECT_TAB_START` + `RelocateVectorTable()` | `Firmware/Core/Src/main.c` | 逻辑照搬，地址改成 SRAM 内 256 B 对齐区；G0 同样支持 `SCB->VTOR` |
| `RAMRO const float …` 常量池 | `main.c` 或 `algorithm_control.c` | 数值原样保留；无 RAM 优化时可去掉 `RAMRO` |
| `ADC_VREF 3.300f` | `main.h` 或 `conv_adc.h` | 按本板实际基准电压改（本工程已用 3.3 V） |
| `MAGIC 0x01919810U` | 本工程无 RTC 备份域用途 | **不需要** |
| `INITIAL_HRTIM_PERIOD/DEADTIME`、`REC_HRTIM_DEADTIME` | `conv_pwm.c` → TIM1 `ARR` / `BDTR.DTG` | 数值需按 64 MHz 时钟与目标开关频率重算 |
| `STM32G474_OverClock()` | — | **不适用**，本工程 64 MHz 由 CubeMX 配置 |
| `RTC_Restart_Init()` | — | G4 Errata 2.2.6 的补丁，本工程无此问题，**不需要** |
| `HAL_TIM_PeriodElapsedCallback(TIM7→HAL_IncTick)` | — | 本工程 SysTick 已自动维护 tick，**不需要** |

## 4. 卸磨杀驴清单

```
Firmware/APP/PowerControl/REF_main_h_main_c.md   ← 本文件（摘录内容已落到 main.h/main.c 后删除）
```

完整清理命令（连同前一轮的两份参考文件）：

```bash
git rm Firmware/APP/PowerControl/stm32g4xx_it.c \
       Firmware/APP/PowerControl/stm32g474xx_flash.sct \
       Firmware/APP/PowerControl/README_RAM_EXEC.md \
       Firmware/APP/PowerControl/REF_main_h_main_c.md
```

## 5. 出处

- `NUEDC_2026A/Firmware_0/Core/Inc/main.h`（157 行，摘录 93–95、121–151）
- `NUEDC_2026A/Firmware_0/Core/Src/main.c`（366 行，摘录 60–79、106–126、128–159、188–197）
