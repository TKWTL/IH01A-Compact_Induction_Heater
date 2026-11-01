#ifndef __CONV_LOOP_H__
#define __CONV_LOOP_H__

#include "algorithm_control.h"
#include "algorithm_filtering.h"
#include "services.h"

//===== 整流侧 RMS 窗口（固定 50Hz 电网输入）=====
#define PFC_AC_FREQ     50
#define PFC_RMS_LEN     (DEFAULT_FSW_KHZ * 1000 / PFC_AC_FREQ)  // 20000/50 = 400点

//===== 逆变侧 RMS 窗口（兼容 30/60Hz：30Hz半周期 = 60Hz全周期 = 333点）=====
#define V_RMS_LEN       (DEFAULT_FSW_KHZ * 1000 / 30 / 2)  // 20000/60 = 333
#define I_RMS_LEN       V_RMS_LEN

//===== 整流器控制器声明（单相全桥 PFC）=====
extern ST_PID  REC_V_PID;      //母线电压外环 PI
extern ST_PR   REC_I_PR;       //输入电流内环 PR (50Hz)

//===== 逆变器控制器声明（三相逆变）=====
extern ST_PR   INV_VAB_PR;     //AB线电压外环 PR
extern ST_PR   INV_VBC_PR;     //BC线电压外环 PR
extern ST_PR   INV_IA_PR;      //A相电流内环 PR
extern ST_PR   INV_IC_PR;      //C相电流内环 PR

//===== VBUS 多重陷波器（50/100/150/200Hz）=====
extern ST_NOTCH           PFC_VBUS_NOTCH_STAGE[4];
extern ST_NOTCH_CASCADE   PFC_VBUS_NOTCH;
float PFC_Notch(float x);

//===== PF 延迟查表 =====
#define PF_TABLE_LEN    21
extern const uint8_t pf_lag_tab[PF_TABLE_LEN];
uint8_t pf_to_lag(uint8_t pf_x100);

//===== 有效值（供外部模块使用）=====
extern float Vin_rms;                   //输入电压有效值
extern float IA_rms, IC_rms;            //输出电流有效值（A/C相）
extern float Vbus_notched;              //母线电压陷波后值
extern SlideRms_t Vin_rms_inst, IA_rms_inst, IC_rms_inst;
extern float Vin_buf[], IA_buf[], IC_buf[];

//===== 环路状态变量（供 MainLoop 清除）=====
extern float soft_k;                    //软启动系数
extern float thph_offset;              //调制偏置点

//===== 控制环路函数 =====
void REC_Loop(float Vo_ref, float Vbus_notched, float PF);  //单相全桥 PFC 整流
void INV_Loop(float Vo_ref, float Vo_ref2, float sin0, float sin240, float Va_calc, float Vc_calc, float fo); //三相逆变

#endif
