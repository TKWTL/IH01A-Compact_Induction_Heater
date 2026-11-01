/*******************************************************************************

*******************************************************************************/
#include "conv_protection.h"
#include "conv_adc.h"
#include "conv_loop.h"
#include <math.h>
#include <armdsp.h>

//在环路中运行的软件保护函数
inline void Protection(void){
    //母线欠压保护
    if (Vbus_notched < 40.0f) {
        PWM_Stop();
        g_conv_state = CONV_FAULT;
        Notification_Send_FromISR("BUS UnderVolt!!");
        return;
    }
    //输入欠压保护
    if (Vin_rms < 28.0f) {
        PWM_Stop();
        g_conv_state = CONV_FAULT;
        Notification_Send_FromISR("Input UnderVolt!!");
        return;
    }
    /*if(fabsf(ReadControlVar(&VZS_t)) > OOV){
            PWM_Stop();
            g_conv_state = CONV_FAULT;
            Notification_Send_FromISR("U Over Current!!");
    }*/
}

/*******************************************************************************
 * FLT1 过流中断服务函数 — 由 stm32g4xx_it.c 中的 HRTIM1_FLT_IRQHandler 调用
 * FLT1 硬件已自动封锁所有 HRTIM 输出，软件处理状态切换与通知
 ******************************************************************************/
void HRTIM_FLT_IRQHandler(void)
{
    //仅处理 FLT1
    if (LL_HRTIM_IsActiveFlag_FLT1(HRTIM1) == 0U) return;
    
    LL_HRTIM_ClearFlag_FLT1(HRTIM1);

    LL_HRTIM_DisableIT_FLT1(HRTIM1);
    
    //软件再确保一次 PWM 关闭
    PWM_Stop();
    
    //状态机：在 RUN/PEND 时跳故障，STOP 时不切换
    if (g_conv_state == CONV_RUN || g_conv_state == CONV_PEND) {
        g_conv_state = CONV_FAULT;
        Notification_Send_FromISR("Over Current!!");
    }
    
}

/**********************硬件保护&保护参数设置区*********************************/
//与设置项有关的可调参数变量
int UpperLimit_VBUS = 72; //母线电压上限，单位V，超过该值会触发保护

//初始化保护参数计算，这是给AWD实现的硬件保护提供的，需要转换成对应的无符号整数
//会打断转换器运行
void Protection_Init1(ui_t *ui){
    uint16_t RegTemp;
    uint8_t ADC_RUN_STA = 0;
    
    if(g_conv_state == CONV_RUN || g_conv_state == CONV_PEND) g_conv_state = CONV_STOP;                      //强制转换器停止运行
    
    //保存当前HRTIM ADC触发配置，避免后续硬编码出错
    uint32_t trig_update = LL_HRTIM_GetADCTrigUpdate(HRTIM1, LL_HRTIM_ADCTRIG_1);
    uint32_t trig_src     = LL_HRTIM_GetADCTrigSrc(HRTIM1, LL_HRTIM_ADCTRIG_1);
    
    //G4不允许在ADC转换过程中修改看门狗阈值
    //通过关闭触发的方式保证DMA搬运序列的正确性
    if(ADC1->CR & ADC_CR_ADSTART){
        ADC_RUN_STA = 1;
        LL_HRTIM_SetADCTrigSrc(HRTIM1, LL_HRTIM_ADCTRIG_1, LL_HRTIM_ADCTRIG_SRC13_NONE);// 立刻禁止后续外部触发
        // 若 EOS 已置位（ADC 空闲），跳过等待；否则等待当前序列完成
        if((ADC1->ISR & ADC_ISR_EOS) == 0U){
            ADC1->ISR = ADC_ISR_EOS;                   // 清空可能存在的脏标志
            while ((ADC1->ISR & ADC_ISR_EOS) == 0U) {} // 等待本次序列结束
        }
        ADC1->CR |= ADC_CR_ADSTP;
        while (ADC1->CR & ADC_CR_ADSTP) {}
    }
    
    //利用VBUS_t当前校准系数反向计算：ADC_count = ((Vbus - zero) / slope) * (4096 / VREF)
    RegTemp = (uint16_t)(((float)UpperLimit_VBUS - VBUS_t.zero) / VBUS_t.slope * (4096.0f / ADC_VREF));
    
    //通过HAL配置AWD1高阈值（保持其他配置不变）
    ADC_AnalogWDGConfTypeDef AnalogWDGConfig = {
        .WatchdogNumber = ADC_ANALOGWATCHDOG_1,
        .WatchdogMode   = ADC_ANALOGWATCHDOG_SINGLE_REG,
        .Channel        = ADC_CHANNEL_3,
        .ITMode         = ENABLE,
        .HighThreshold  = RegTemp,
        .LowThreshold   = 0,
        .FilteringConfig = ADC_AWD_FILTERING_NONE
    };
    HAL_ADC_AnalogWDGConfig(&hadc1, &AnalogWDGConfig);
    
    //如果关闭了ADC记得重启，并从保存的配置恢复触发
    if(ADC_RUN_STA){
        ADC1->CR |= ADC_CR_ADSTART;
        LL_HRTIM_ConfigADCTrig(HRTIM1, LL_HRTIM_ADCTRIG_1, trig_update, trig_src);
    }
}

//ADC看门狗回调函数，STM32G474的AWDT只支持单门限触发，用途十分有限
void HAL_ADC_LevelOutOfWindowCallback(ADC_HandleTypeDef *hadc){
    if(g_conv_state == CONV_RUN || g_conv_state == CONV_PEND){//在 RUN/PEND 时触发
        if (hadc->Instance == ADC1){   
            PWM_Stop();
            g_conv_state = CONV_FAULT;
            Notification_Send_FromISR("BUS Over Voltage!!");
        }
    }
}

//初始化保护参数计算，这是给DAC+COMP实现的硬件保护提供的，需要转换成对应的无符号整数
//不会打断转换器运行
void Protection_Init2(ui_t *ui){
    
}

//以下函数须在ui_conf.c中调用
ui_item_t VBUSLimit_Item;
//创建保护设置模块所需的参数
void Create_Protection_Parameters1(ui_t *ui){
    static ui_data_t VBUSLimit_data;
    VBUSLimit_data.name = "VBUS Limit";
    VBUSLimit_data.ptr = &UpperLimit_VBUS;
    VBUSLimit_data.function = Protection_Init1;
    VBUSLimit_data.functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
    VBUSLimit_data.dataType = UI_DATA_INT;
    VBUSLimit_data.actionType = UI_DATA_ACTION_RW;
    VBUSLimit_data.max = 75;
    VBUSLimit_data.min = 24;
    VBUSLimit_data.step = 1;
    static ui_element_t VBUSLimit_element;
    VBUSLimit_element.data = &VBUSLimit_data;
    Create_element(&VBUSLimit_Item, &VBUSLimit_element);
}
//将保护设置模块的对象添加到菜单中
void Add_ProtectionLevel_Items(ui_page_t *ParentPage){
    AddItem(" VBUS Voltage Limit", UI_ITEM_DATA, NULL, &VBUSLimit_Item, ParentPage, NULL, NULL);
}

/************************触发保护后的自动恢复功能区****************************/
//与设置项有关的可调参数变量
uint8_t AutoRecoverEN = 0;                                                      //自动恢复使能，0：不会自动恢复；1：试图自动恢复
int AutoRecoverDelay = 3000;                                                    //自动恢复延迟，单位毫秒
int AutoRecoverTime = 3;                                                        //自动重试次数，为0时会无限尝试

//重试次数计数器
int AutoRecoverCounter = 0;

/******************** FLT1 恢复监控（非阻塞轮询）***************************/
//在 AutoRecover_Task 中周期调用，检测 FLT1 引脚是否恢复高电平
//注意：不依赖 g_conv_state，FLT 在 STOP 状态也需能触发
void FLT1_Monitor(void)
{
    static uint8_t stable_cnt = 0;
    
    if (LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_12) != 0) {
        //FLT1 已恢复为高电平（故障解除），持续确认防抖动
        stable_cnt++;
        if (stable_cnt >= 3) {  // 3 × 200ms = 600ms 稳定后操作
            stable_cnt = 0;
            if (LL_HRTIM_IsActiveFlag_FLT1(HRTIM1)) {
                LL_HRTIM_ClearFlag_FLT1(HRTIM1);
                //重新使能 FLT1 中断（确保下次故障仍可触发）
                LL_HRTIM_EnableIT_FLT1(HRTIM1);
            }
        }
    } else {
        stable_cnt = 0;
    }
}

//自动恢复任务函数，CubeMX FreeRTOS也要配置添加该任务
void AutoRecover_Task(void *argument){
    for(;;){
        FLT1_Monitor();  // 独立于转换器状态的 FLT 轮询
        
        if(g_conv_state == CONV_FAULT){
            if(AutoRecoverEN){
                if(AutoRecoverTime && AutoRecoverCounter >= AutoRecoverTime){   //重试次数为0时不触发防循环机制
                    g_conv_state = CONV_STOP;   //放弃重启
                    AutoRecoverCounter = 0;     //复位计数器
                    continue;
                }
                osDelay(AutoRecoverDelay);      //进行延迟
                if(g_conv_state == CONV_FAULT){
                    if(AutoRecoverTime) AutoRecoverCounter++;//累加重试次数计数器
                    g_conv_state = CONV_PEND;   //重新等待过零点
                }
            }
            else{
                g_conv_state = CONV_STOP;
            }
        }
        
        osDelay(200);
    }
}

//以下函数须在ui_conf.c中调用
ui_item_t AutoRecoverEN_Item, AutoRecoverDelay_Item, AutoRecoverTime_Item;
//创建保护设置模块所需的参数
void Create_Protection_Parameters2(ui_t *ui){
    static ui_data_t AutoRecoverEN_data;
    AutoRecoverEN_data.name = "Auto Recover Enable";
    AutoRecoverEN_data.ptr = &AutoRecoverEN;
    AutoRecoverEN_data.dataType = UI_DATA_SWITCH;
    AutoRecoverEN_data.actionType = UI_DATA_ACTION_RW;
    static ui_element_t AutoRecoverEN_element;
    AutoRecoverEN_element.data = &AutoRecoverEN_data;
    Create_element(&AutoRecoverEN_Item, &AutoRecoverEN_element);
    
    static ui_data_t AutoRecoverDelay_data;
    AutoRecoverDelay_data.name = "Restart Delay in ms";
    AutoRecoverDelay_data.ptr = &AutoRecoverDelay;
    AutoRecoverDelay_data.dataType = UI_DATA_INT;
    AutoRecoverDelay_data.actionType = UI_DATA_ACTION_RW;
    AutoRecoverDelay_data.max = 10000;
    AutoRecoverDelay_data.min = 1000;
    AutoRecoverDelay_data.step = 500;
    static ui_element_t AutoRecoverDelay_element;
    AutoRecoverDelay_element.data = &AutoRecoverDelay_data;
    Create_element(&AutoRecoverDelay_Item, &AutoRecoverDelay_element);
    
    static ui_data_t AutoRecoverTime_data;
    AutoRecoverTime_data.name = "Restart Times";
    AutoRecoverTime_data.ptr = &AutoRecoverTime;
    AutoRecoverTime_data.dataType = UI_DATA_INT;
    AutoRecoverTime_data.actionType = UI_DATA_ACTION_RW;
    AutoRecoverTime_data.max = 10;
    AutoRecoverTime_data.min = 0;
    AutoRecoverTime_data.step = 1;
    static ui_element_t AutoRecoverTime_element;
    AutoRecoverTime_element.data = &AutoRecoverTime_data;
    Create_element(&AutoRecoverTime_Item, &AutoRecoverTime_element);
}
//将保护设置模块的对象添加到菜单中
void Add_Protection_Items(ui_page_t *ParentPage){
    AddItem(" Auto Recover Enable", UI_ITEM_DATA, NULL, &AutoRecoverEN_Item, ParentPage, NULL, NULL);
    AddItem(" Auto Recover Delay", UI_ITEM_DATA, NULL, &AutoRecoverDelay_Item, ParentPage, NULL, NULL);
    AddItem(" Auto Recover Times", UI_ITEM_DATA, NULL, &AutoRecoverTime_Item, ParentPage, NULL, NULL);
}

/***************************创建保护选项设置页面*******************************/
#include "image.h"
ui_page_t Protection_Page;
ui_item_t Protection_Item, ProtectionHead_Item;

void Add_Protection_Page(ui_page_t *ParentPage){
    AddItem("-Protection Set", UI_ITEM_PARENTS, img_shield, &Protection_Item, ParentPage, &Protection_Page, NULL);
        AddPage("[Setting]", &Protection_Page, UI_PAGE_TEXT, ParentPage);
            AddItem("[Menu]", UI_ITEM_RETURN, NULL, &ProtectionHead_Item, &Protection_Page, ParentPage, NULL);
            Add_ProtectionLevel_Items(&Protection_Page);
            Add_Protection_Items(&Protection_Page);
}
    