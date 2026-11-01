#include "conv_loop.h"
#include "conv_controller.h"
#include "conv_adc.h"
#include "conv_pwm.h"
#include "algorithm_control.h"
#include "algorithm_filtering.h"
#include <math.h>
#include <armdsp.h>

/*******************************************************************************
 * 注意：所有带状态的控制器（PID/PR/陷波器）在此处用 = {...} 完成
 *       常量参数的静态初始化，差分方程系数等运行时参数在
 *       Converter_Init 中调用对应的 Init 函数计算。
 ******************************************************************************/

//========== 整流器：母线电压 PI 外环 ==========
RAMVAR ST_PID REC_V_PID = {
    .fpKp = 0.5f,
    .fpKi = 5.0f,
    .fpKd = 0.0f,
    .fpDt = CTRL_DT,
    .fpKaw = 1.0f   // 反算抗饱和系数
};

//========== 整流器：输入电流 PR 内环（固定 50Hz 电网）==========
RAMVAR ST_PR REC_I_PR = {
    .omiga_c = 1.256f,
    .omiga_0 = 2 * 3.1416f * PFC_AC_FREQ,  // 314.16 rad/s, 固定50Hz
    .fpDt    = CTRL_DT,
    .Kp      = 1.5f,
    .Kr      = 40.0f
};

//========== VBUS 多重陷波器（50/100/150/200Hz，Q=20）==========
#define PFC_NOTCH_FS    (DEFAULT_FSW_KHZ * 1000.0f)
#define PFC_NOTCH_Q     20.0f
RAMVAR ST_NOTCH PFC_VBUS_NOTCH_STAGE[4] = {
    {.fs = PFC_NOTCH_FS, .f0 =  50.0f, .Q = PFC_NOTCH_Q},
    {.fs = PFC_NOTCH_FS, .f0 = 100.0f, .Q = PFC_NOTCH_Q},
    {.fs = PFC_NOTCH_FS, .f0 = 150.0f, .Q = PFC_NOTCH_Q},
    {.fs = PFC_NOTCH_FS, .f0 = 200.0f, .Q = PFC_NOTCH_Q}
};
RAMVAR ST_NOTCH_CASCADE PFC_VBUS_NOTCH = {
    .stage = PFC_VBUS_NOTCH_STAGE,
    .num   = 4
};

inline float PFC_Notch(float x)
{
    x = Notch_Calc(&PFC_VBUS_NOTCH_STAGE[0], x);
    x = Notch_Calc(&PFC_VBUS_NOTCH_STAGE[1], x);
    x = Notch_Calc(&PFC_VBUS_NOTCH_STAGE[2], x);
    x = Notch_Calc(&PFC_VBUS_NOTCH_STAGE[3], x);
    return x;
}

//========== PF 延迟查表（@20kHz/50Hz，400点/周期）==========
#define INPUT_CAP_COMP_LAG  2   // 补偿10µF输入滤波电容的容性无功 (36V/50Hz → 0.113A → 2.0°)

RAMRO const uint8_t pf_lag_tab[PF_TABLE_LEN] = {
     0,  9, 13, 16, 18, 20, 22, 24, 26, 27, 29,
    30, 32, 33, 34, 35, 37, 38, 39, 40, 41
};

inline uint8_t pf_to_lag(uint8_t pf_x100)
{
    if(pf_x100 > 100) pf_x100 = 100;
    if(pf_x100 < 80)  pf_x100 = 80;
    return pf_lag_tab[100 - pf_x100];
}

//========== 有效值统计（整流器输入电压 + 逆变器输出电流）==========
RAMVAR float Vin_rms = 36.0f;
RAMVAR float IA_rms  = 0.0f;
RAMVAR float IC_rms  = 0.0f;
RAMVAR float Vbus_notched = 50.0f;

RAMVAR float Vin_buf[PFC_RMS_LEN];                // 400点 @20kHz = 50Hz 整周期
RAMVAR float IA_buf[I_RMS_LEN];
RAMVAR float IC_buf[I_RMS_LEN];
RAMVAR SlideRms_t Vin_rms_inst;
RAMVAR SlideRms_t IA_rms_inst;
RAMVAR SlideRms_t IC_rms_inst;

//========== 环路状态变量 ==========
RAMVAR float soft_k = 0.0f;         // 软启动系数 (0→1)
RAMVAR float thph_offset = MODULATION_START;  // 调制偏置点

/*******************************************************************************
 * REC_Loop — 单相全桥 PFC 整流控制
 * 外环：母线电压 PI → 电流幅值指令（待输出电压稳定后启用）
 * 内环：输入电流 PR（50Hz）→ 占空比
 * 电网同步：直接取输入电压 VN 1有效值化作为 sin(θ_grid)
 ******************************************************************************/
inline void REC_Loop(float Vo_ref, float Vbus_notched, float PF)
{
    float Vbus_err = Vo_ref - Vbus_notched;

    //===== 电压外环 PI =====
    float I_amplitude = PID_Calc(&REC_V_PID, Vbus_err, -0.5f, 5.0f);

    //===== 电网相位（PF 移相：滑动窗口延迟读取实现无功可调）=====
    float VN_inst = ReadControlVar(&VN_t);
    uint8_t lag = pf_to_lag((uint8_t)(PF * 100.0f));            // 1.00→0.80 映射 lag 0→41
    lag += INPUT_CAP_COMP_LAG;                                   // 补偿输入滤波电容容性无功
    DAC3_Set((uint32_t)(lag* 80.0f));  // IN(反相) → DAC3
    float VN_delayed = SlideRms_ReadDelayed(&Vin_rms_inst, lag);
    float grid_sin = VN_delayed / Vin_rms;                       // 含移相的1有效值正弦

    //===== 电流指令 =====
    float I_ref = I_amplitude * grid_sin;

    //===== 电流内环 PR =====
    REC_I_PR.fpDes = I_ref;
    REC_I_PR.fpFB  = -ReadControlVar(&IN_t);
    // 抗饱和反算：PR 对称钳位于 [VN-Vbus+2, VN+Vbus-2] 较窄侧
    REC_I_PR.fpLim = Vbus_notched - fabsf(VN_inst) - 2.0f;
    if (REC_I_PR.fpLim < 0.0f) REC_I_PR.fpLim = 0.0f;
    PR_Control(&REC_I_PR);

    //===== 占空比输出（电网电压前馈解耦，SetDuty_Rec 内部处理饱和）=====
    float D_eff = 0.5f * (0.99f * VN_inst - REC_I_PR.output_fpU) * inv_VBUS;
    int diode_clamp = SetDuty_Rec(D_eff);

    if(diode_clamp == 0) PWM_Start_Rec();
}

/*******************************************************************
 * DPWM-A零序注入 + 占空比映射（宏函数，避免指针间接访问）
 * 输入 ma/mb/mc ∈ [-1/√3, 1/√3]（归一化电压），输出 duty_a/b/c ∈ [0, 1]
 * 特性：ma=1/√3 时最终输出相电压 Vbus/√3
 * desmos可视化：https://www.desmos.com/calculator/o2enslci2l
 * 该注入式调制方法由 渠嘉琛 提出，仅供非盈利及学术研究使用，未经许可不得用于商业用途
 ******************************************************************/
#define DPWMA_MODULATE(ma, mb, mc, offset) do {                     \
    float a0_, a1_, b0_, b1_, c0_, c1_;                             \
    if ((ma) > 0.0f) { a0_ = 0.5f - (ma); a1_ = (ma); }             \
    else             { a0_ = -(ma);       a1_ = 0.5f + (ma); }      \
    if ((mb) > 0.0f) { b0_ = 0.5f - (mb); b1_ = (mb); }             \
    else             { b0_ = -(mb);       b1_ = 0.5f + (mb); }      \
    if ((mc) > 0.0f) { c0_ = 0.5f - (mc); c1_ = (mc); }             \
    else             { c0_ = -(mc);       c1_ = 0.5f + (mc); }      \
    float z0_ = fminf(fminf(a0_, b0_), c0_);                        \
    float z1_ = fminf(fminf(a1_, b1_), c1_);                        \
    float zero_seq_ = (z0_ > z1_) ? (-z1_) : z0_;                   \
    (ma) = (offset) + (ma) + zero_seq_;                             \
    (mb) = (offset) + (mb) + zero_seq_;                             \
    (mc) = (offset) + (mc) + zero_seq_;                             \
} while(0)

/*******************************************************************
 * SVPWM 连续调制（零序注入 = −(Vmax+Vmin)/2）
 * 输入 ma/mb/mc（归一化相电压），输出 duty ∈ [0, 1]
 * 三相均连续开关，自举电容每周期刷新，适合低频带载
 ******************************************************************/
#define SVPWM_MODULATE(ma, mb, mc, offset) do {                     \
    float vmax_ = fmaxf(fmaxf((ma), (mb)), (mc));                   \
    float vmin_ = fminf(fminf((ma), (mb)), (mc));                   \
    float zero_seq_ = -0.5f * (vmax_ + vmin_);                      \
    (ma) = (offset) + (ma) + zero_seq_;                             \
    (mb) = (offset) + (mb) + zero_seq_;                             \
    (mc) = (offset) + (mc) + zero_seq_;                             \
} while(0)

//========== 逆变器：线电压 PR 外环 (AB / BC) ==========
RAMVAR ST_PR INV_VAB_PR = {
    .omiga_c = 0.314f,
    .omiga_0 = DEFAULT_AC_OMEGA,  // 运行时由 Converter_Init 按 fo 重算
    .fpDt    = CTRL_DT,
    .Kp      = 0.05f,
    .Kr      = 128.0f
};
RAMVAR ST_PR INV_VBC_PR = {
    .omiga_c = 0.314f,
    .omiga_0 = DEFAULT_AC_OMEGA,
    .fpDt    = CTRL_DT,
    .Kp      = 0.05f,
    .Kr      = 128.0f
};

//========== 逆变器：相电流 PR 内环 (A / C) ==========
RAMVAR ST_PR INV_IA_PR = {
    .omiga_c = 3.142f,
    .omiga_0 = DEFAULT_AC_OMEGA,
    .fpDt    = CTRL_DT,
    .Kp      = 1.0f,
    .Kr      = 40.0f
};
RAMVAR ST_PR INV_IC_PR = {
    .omiga_c = 3.142f,
    .omiga_0 = DEFAULT_AC_OMEGA,
    .fpDt    = CTRL_DT,
    .Kp      = 1.0f,
    .Kr      = 40.0f
};

/*******************************************************************************
 * INV_Loop — 三相逆变控制（电压外环 + 电流内环，DPWM-A 调制）
 * sin0 / sin120 / sin240 由 CORDIC 提供，对应 sin(θ), sin(θ+120°), sin(θ+240°)
 ******************************************************************************/
inline void INV_Loop(float Vo_ref, float Vo_ref2, float sin0, float sin240, float Va_calc, float Vc_calc, float fo)
{
    //===== 软启动与调制偏置 =====
    Vo_ref  = soft_k * Vo_ref  + 0.05f;
    Vo_ref2 = soft_k * Vo_ref2 + 0.01f;
    if (soft_k < 1.0f)
        soft_k += SOFTSTART_SLOPE;
    if (thph_offset < MODULATION_MIDPOINT)
        thph_offset += MODULATION_SLOPE;

    //===== 线电压 RMS → 相电压峰值 =====
    float Vp_ref  = Vo_ref  * 1.4142136f / 1.732051f;   // √2 / √3, A 相
    float Vp_ref2 = Vo_ref2 * 1.4142136f / 1.732051f;   // √2 / √3, C 相

    // 相电压瞬时参考（仅 A/C 相，B 相由三相平衡导出）
    float Va_ref = Vp_ref  * sin0;
    float Vc_ref = Vp_ref2 * sin240;

    float V_pr_lim = 4.0f;
    //===== 电压外环 PR（相电压），输出为电流参考 (A) =====
    INV_VAB_PR.fpDes = Va_ref;
    INV_VAB_PR.fpFB  = Va_calc;
    INV_VAB_PR.fpLim = V_pr_lim;
    PR_Control(&INV_VAB_PR);

    INV_VBC_PR.fpDes = Vc_ref;
    INV_VBC_PR.fpFB  = Vc_calc;
    INV_VBC_PR.fpLim = V_pr_lim;
    PR_Control(&INV_VBC_PR);

    //===== 电流内环 PR（相电流）=====
    // 电压环输出 → 电流指令
    float IA_ref = INV_VAB_PR.output_fpU;
    float IC_ref = INV_VBC_PR.output_fpU;
    float I_pr_lim = 4.0f;

    INV_IA_PR.fpDes = IA_ref;
    INV_IA_PR.fpFB  = ReadControlVar(&IA_t);
    INV_IA_PR.fpLim = I_pr_lim;  // 电流环抗饱和限幅
    PR_Control(&INV_IA_PR);

    INV_IC_PR.fpDes = IC_ref;
    INV_IC_PR.fpFB  = ReadControlVar(&IC_t);
    INV_IC_PR.fpLim = I_pr_lim;
    PR_Control(&INV_IC_PR);

    //===== 电流环输出 → 占空比（参考电压前馈解耦，无测量噪声）=====
    // V_inv_ref = PR_output + V_ref → D = (PR_output + V_ref) / Vbus
    float gain = inv_VBUS;
    float Du = (INV_IA_PR.output_fpU + Va_ref) * gain;
    float Dw = (INV_IC_PR.output_fpU + Vc_ref) * gain;
    float Dv = -(Du + Dw);

    // 调制方式自适应：≥60Hz → DPWM（高效钳位），<60Hz → SVPWM（防自举欠压）
    if (fo >= 60.0f) {
        DPWMA_MODULATE(Du, Dv, Dw, thph_offset);
    } else {
        SVPWM_MODULATE(Du, Dv, Dw, thph_offset);
    }

    SetDuty_Inv(Du, Dv, Dw);
}
