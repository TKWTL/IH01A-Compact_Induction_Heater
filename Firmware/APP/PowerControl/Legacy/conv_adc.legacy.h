#ifndef __CONV_ADC_H__
#define __CONV_ADC_H__

#ifdef __cplusplus
extern "C" {
#endif
    
#include "services.h"
    
typedef struct {
    volatile uint16_t *pBuffer;              // 指向数据缓冲区的指针，存放原始采样数据或控制信号序列
    uint16_t channel;               // 对应的通道编号，可以是 ADC 
    float slope;                    // 线性补偿斜率（增益），用于数据校正
    float zero;                     // 线性补偿零点（偏置），用于数据校正
    float result;
}PowerControlVar_t;

extern volatile uint16_t ADC1_Buffer[];
extern volatile uint16_t ADC2_Buffer[];
extern volatile uint16_t ADC3_Buffer[];
extern volatile uint16_t ADC4_Buffer[];

extern PowerControlVar_t  
    VBUS_t, //母线电压，单位V
    IA_t,   //逆变A相电流，单位A
    IB_t,   //逆变B相电流，单位A
    IC_t,   //逆变C相电流，单位A
    VAB_t,  //逆变A-B线电压，单位V
    VBC_t,  //逆变B-C线电压，单位V
    VZS_t,  //三相零序对地电压，单位V
    IN_t,   //整流器输入电流，单位A（极性：输入→半桥为负）
    VN_t,   //整流器输入电压，单位V
    VCM_t;  //整流器共模电压，单位V

//环路ISR一开始就必须执行的参数加载函数（将放入CCM RAM中加速运行）
void LoadControlVar();
//从结构体中配置好的参数中取得数值并以float格式输出
float ReadControlVar(PowerControlVar_t *p_var);

//启动ADC的DMA转换
void ADC_Init(void);


//自动校准函数
void AutoCalibration(ui_t *ui);

//以下函数须在ui_conf.c中调用
//创建参数补偿模块所需的参数
void Create_ADC_Parameters(ui_t *ui);
//将参数补偿模块的对象添加到菜单中
void Add_ADC_Items(ui_page_t *ParentPage);

#ifdef __cplusplus
}
#endif

#endif