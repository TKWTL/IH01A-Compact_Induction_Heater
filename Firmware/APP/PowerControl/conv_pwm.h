#ifndef __CONV_PWM_H__
#define __CONV_PWM_H__

#ifdef __cplusplus
extern "C" {
#endif
    
#include "services.h"
    
extern int SwitchingFrequency;
    
//启动定时器与频率基准
void Timer_Init(ui_t *ui);
    
//设置开关频率
void SetFrequency(ui_t *ui);
    
//设置占空比，根据周期和输入的百分比值，带占空比限幅
int SetDuty_Rec(float Dr);
void SetDuty_Inv(float Du, float Dv, float Dw);

//打开PWM
void PWM_Start_Rec(void);
void PWM_Start_Inv(void);
//关闭PWM
void PWM_Stop(void);

//DAC快捷输出（无符号12位整数，不限幅）
inline void DAC1_Set(uint32_t val) { LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_1, val); }
inline void DAC2_Set(uint32_t val) { LL_DAC_ConvertData12RightAligned(DAC1, LL_DAC_CHANNEL_2, val); }
inline void DAC3_Set(uint32_t val) { LL_DAC_ConvertData12RightAligned(DAC2, LL_DAC_CHANNEL_1, val); }


/***********************以下函数须在ui_conf.c中调用****************************/
//创建PWM设置模块所需的参数
void Create_PWM_Parameters(ui_t *ui);
//将PWM设置模块的对象添加到菜单中
void Add_PWM_Items(ui_page_t *ParentPage);

#ifdef __cplusplus
}
#endif

#endif
