/*******************************************************************************
负责控制器的具体运行逻辑
*******************************************************************************/
#include "conv_controller.h"
#include "conv_loop.h"
#include "conv_adc.h"
#include "algorithm_control.h"
#include "algorithm_filtering.h"
#include "conv_pwm.h"
#include "dashboard.h"

#define LOOP_FREQ   (DEFAULT_FSW_KHZ * 1000)   // 控制环路执行频率 Hz
#define PHASE_BITS  32                          // 相位累加器位宽

uint32_t PhaseACC = 0;
uint32_t fcw = (uint32_t)(((uint64_t)DEFAULT_AC_FREQ << PHASE_BITS) / LOOP_FREQ);

float sin0, sin120, sin240;                     // CORDIC 三相正弦基准

RAMVAR ConvStatus_t g_conv_state = CONV_STOP;                   //转换器正在运行标志变量

RAMVAR float inv_VBUS;                                          //母线电压的倒数，用于标幺化计算

static float Vrms_filt = 36.0f;                                 // Vin_rms 滤波值（−20dB@10Hz, α=3.157e-4）

/*******************************环路核心函数***********************************/
//取采样值，环路计算，保护，周期性统计
inline void MainLoop(void){
    LL_GPIO_SetOutputPin(TEST1_GPIO_Port, TEST1_Pin);//拉高IO用于测试执行时间
    
    LoadControlVar();//从ADC采样后的数组中取得控制用的采样量
    SlideRms_Store(&Vin_rms_inst, ReadControlVar(&VN_t)); // 提前存储供 PF 延时读取
    inv_VBUS = 1.0f / ReadControlVar(&VBUS_t);
    
    // CORDIC 读取上周期写入的相位计算结果（已就绪，零等待）
    sin0   = CORDIC_Read();
    sin120 = CORDIC_Get_sin120();
    sin240 = CORDIC_Get_sin240();

    // 相电压解算（线→相，供电压外环反馈）
    float Vab = ReadControlVar(&VAB_t);
    float Vbc = ReadControlVar(&VBC_t);
    float Va_calc = (Vab* 2 + Vbc)/ 3.0f;
    float Vc_calc = (-Vab - 2* Vbc)/ 3.0f;

    if(g_conv_state == CONV_RUN){
        //Protection();//保护函数

        // PFC 整流控制
        //float Vbus_ref = fmaxf(VO_Set, Vrms_filt) * SQRT2_F32 + 4.0f;
        float Vbus_ref = 62.0f;
        REC_Loop(Vbus_ref, Vbus_notched, PF_Set);

        // 逆变器双闭环 PR 控制
        INV_Loop(VO_Set, VO_Set2, sin0, sin240, Va_calc, Vc_calc, fo_Set);
        PWM_Start_Inv();
    }
    else{
        //停止输出
        PWM_Stop();
        //清除积分器状态
        PID_Reset(&REC_V_PID);
        PR_Clean(&REC_I_PR);
        PR_Clean(&INV_VAB_PR);
        PR_Clean(&INV_VBC_PR);
        PR_Clean(&INV_IA_PR);
        PR_Clean(&INV_IC_PR);
        //清除软启动状态
        soft_k = 0;
        // 停机时持续跟踪零序电压，启动后 DPWM 接管
        thph_offset = ReadControlVar(&VZS_t) * inv_VBUS;
        if (thph_offset < 0.0f) thph_offset = 0.0f;
        if (thph_offset > MODULATION_MIDPOINT) thph_offset = MODULATION_MIDPOINT;

        LL_GPIO_ResetOutputPin(TEST0_GPIO_Port, TEST0_Pin);  //变换器运行状态指示，供示波器捕获
    }

    
    //========== 连续有效值统计（始终运行，独立于CONV_RUN状态） ==========
    // Vin_rms_inst 的 Store 已提前到 LoadControlVar 之后（供 PF 延时使用）
    SlideRms_Store(&IA_rms_inst,  ReadControlVar(&IA_t));
    SlideRms_Store(&IC_rms_inst,  ReadControlVar(&IC_t));
    SlideRms_Update(&Vin_rms_inst);
    SlideRms_Update(&IA_rms_inst);
    SlideRms_Update(&IC_rms_inst);
    Vin_rms = Vin_rms_inst.rms;
    IA_rms  = IA_rms_inst.rms;
    IC_rms  = IC_rms_inst.rms;

    Vbus_notched = PFC_Notch(ReadControlVar(&VBUS_t));  //陷波后母线电压
    Vrms_filt = 0.0003157f * Vin_rms + (1.0f - 0.0003157f) * Vrms_filt;// Vin_rms 一阶低通滤波（−20dB @ 10Hz, α=3.157e-4）

    //===== VN 过零点检测：PEND → RUN =====
    //按键启动时母线已建立，等 VN 负→正过零后切入 RUN，避开满调制比区域，
    //避免启动瞬间占空比饱和、电流冲击
    {
        static float VN_prev = 0.0f;
        static ConvStatus_t prev_conv_state = CONV_STOP;
        // 首次进入 PEND（启动 / 故障自动恢复）时清空 VN_prev，
        // 避免残留负值在 VN>0 时误触发过零
        if (g_conv_state == CONV_PEND && prev_conv_state != CONV_PEND) {
            VN_prev = 0.0f;
        }
        prev_conv_state = g_conv_state;

        if (g_conv_state == CONV_PEND) {
            float VN_now = ReadControlVar(&VN_t);
            if (VN_prev < 0.0f && VN_now >= 0.0f) {
                g_conv_state = CONV_RUN;
                LL_GPIO_SetOutputPin(TEST0_GPIO_Port, TEST0_Pin);
            }
            VN_prev = VN_now;
        }
    }
    
    /*if (g_conv_state == CONV_PEND) {
        // 执行 PEND 状态下的逻辑
        g_conv_state = CONV_RUN;
    }*/

    // 预写下一周期相位（本周期执行期间 CORDIC 后台计算，下周期读取零延迟）
    PhaseACC += fcw;
    CORDIC_Write(PhaseACC);

    // 频率变更检测：fo_Set 变化时自动更新 PR 系数
    static float fo_last = DEFAULT_AC_FREQ;
    if (fo_Set != fo_last) {
        ModifyFreqCtrlWord(NULL);
        fo_last = fo_Set;
    }

    //DAC1_Set((uint32_t)(ReadControlVar(&VBUS_t)*62.0f));  // VBUS → DAC1
    //DAC2_Set((uint32_t)(Vbus_notched* 62.0f));  // IN(反相) → DAC2

    LL_GPIO_ResetOutputPin(TEST1_GPIO_Port, TEST1_Pin);//拉低IO用于测试执行时间
}

//修改频率控制字函数，修改PR的中心频率，重新计算所有PR参数
//调用前强制停机，避免运行中改频导致PR状态与新的omiga_0失配
void ModifyFreqCtrlWord(ui_t *ui){
    if (g_conv_state == CONV_RUN || g_conv_state == CONV_PEND) {
        g_conv_state = CONV_STOP;   // 强制停机，下一周期 MainLoop 清零 PR/PID
    }
    float fo = fo_Set;  // 当前输出频率设定
    fcw = (uint32_t)(((uint64_t)fo << PHASE_BITS) / LOOP_FREQ);
    INV_VAB_PR.omiga_0 = 2 * 3.1416f * fo;
    INV_VBC_PR.omiga_0 = INV_VAB_PR.omiga_0;
    INV_IA_PR.omiga_0  = INV_VAB_PR.omiga_0;
    INV_IC_PR.omiga_0  = INV_VAB_PR.omiga_0;
    PR_Init(&INV_VAB_PR);
    PR_Init(&INV_VBC_PR);
    PR_Init(&INV_IA_PR);
    PR_Init(&INV_IC_PR);
}

//开关电源系统初始化——由 ADC_Task 在调度器启动后调用
void Converter_Init(void)
{
    Protection_Init1(&ui);
    Protection_Init2(&ui);
    ADC_Init();
    
    //初始化滑动窗口有效值
    SlideRms_Init(&Vin_rms_inst, Vin_buf, PFC_RMS_LEN);   // 400点 = 50Hz 整周期
    SlideRms_Init(&IA_rms_inst,  IA_buf,  I_RMS_LEN);
    SlideRms_Init(&IC_rms_inst,  IC_buf,  I_RMS_LEN);
    
    //整流器控制器初始化
    PID_Init(&REC_V_PID);
    PR_Init(&REC_I_PR);
    
    //逆变器控制器初始化（按当前 fo 计算 PR 差分系数）
    ModifyFreqCtrlWord(NULL);
    
    //初始化母线电压多重陷波器（逐级计算系数）
    for (uint8_t i = 0; i < PFC_VBUS_NOTCH.num; i++)
        Notch_Init(&PFC_VBUS_NOTCH.stage[i]);
    
    Timer_Init(&ui);
}
/***********************以下函数须在ui_conf.c中调用****************************/
ui_item_t ACFrequency_Item;
//创建模式调整模块所需的参数
void Create_ConvMode_Parameters(ui_t *ui){
    static ui_data_t ACFrequency_data;
    ACFrequency_data.name = "AC Frequency";
    //ACFrequency_data.ptr = &ACFrequency;
    ACFrequency_data.function = ModifyFreqCtrlWord;
    ACFrequency_data.functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
    ACFrequency_data.dataType = UI_DATA_INT;
    ACFrequency_data.actionType = UI_DATA_ACTION_RW;
    ACFrequency_data.max = 100;
    ACFrequency_data.min = 20;
    ACFrequency_data.step = 1;
    static ui_element_t ACFrequency_element;
    ACFrequency_element.data = &ACFrequency_data;
    Create_element(&ACFrequency_Item, &ACFrequency_element);
}
//将模式调整模块的对象添加到菜单中
void Add_ConvMode_Items(ui_page_t *ParentPage){
    //AddItem(" AC Frequency", UI_ITEM_DATA, NULL, &ACFrequency_Item, ParentPage, NULL, NULL);
}

