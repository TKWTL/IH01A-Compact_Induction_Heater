#ifndef __CONV_CONTROLLER_H__
#define __CONV_CONTROLLER_H__

#ifdef __cplusplus
extern "C" {
#endif
    
#include "services.h"
    
/*******************************************************************************
    转换器特性参数配置区
*******************************************************************************/
#define DEFAULT_AC_FREQ     60                                                  //默认的AC逆变频率
#define DEFAULT_AC_OMEGA    2* 3.1416f* DEFAULT_AC_FREQ                         //默认的AC逆变角频率

#define ANTI_WINDUP_LIMIT   0.50f                                               //抗饱和PR算法输出的最大幅值
    
#define MODULATION_START    0.271f                                              //经过测试得出的起始偏置点位置（被实时计算的零序取代）
#define MODULATION_MIDPOINT 0.500f                                              //经过测试得出可以给出最大摆幅的调制偏置点位置
#define MODULATION_SLOPE    0.0004f                                              //每个周期偏置缓启动增长步长，必须大于软启动步长
    
#define SOFTSTART_SLOPE     0.00005f                                             //每个周期幅度软启动增长步长
    
#define DEFAULT_FSW_KHZ     20                                                  //默认的开关频率（单位KHz）
#define CTRL_DT             0.001f/ DEFAULT_FSW_KHZ                             //控制周期 = 50µs @20kHz

typedef enum{
    CONV_STOP,
    CONV_PEND,      //等待 VN 过零点启动
    CONV_RUN,
    CONV_FAULT
}ConvStatus_t;
    
extern ConvStatus_t g_conv_state;//转换器正在运行标志变量
extern float inv_VBUS;

/*******************************环路核心函数***********************************/
//取采样值，环路计算，保护，周期性统计
void MainLoop(void);
void Converter_Init(void);
void ModifyFreqCtrlWord(ui_t *ui); //修改频率控制字

/***********************以下函数须在ui_conf.c中调用****************************/
//创建模式调整模块所需的参数
void Create_ConvMode_Parameters(ui_t *ui);
//将模式调整模块的对象添加到菜单中
void Add_ConvMode_Items(ui_page_t *ParentPage);
    
#ifdef __cplusplus
}
#endif

#endif
