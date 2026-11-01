#include "algorithm_control.h"
#include <math.h>
#include <armdsp.h>

/*******************************************************************************
 * 离散控制函数库
 * 这里的函数理论上有增无减
 * 目录：
 ******************************************************************************/

/*******************************************************************************
 * 比例-积分-微分控制器（PID）计算函数
 * 采用增量式计算
 ******************************************************************************/
void PID_Init(ST_PID *pid)
{
    pid->fpKiDt = pid->fpKi * pid->fpDt;
    pid->fpKdDivDt = pid->fpKd / pid->fpDt;

    pid->fpErr = 0.0f;
    pid->fpPreErr = 0.0f;

    pid->fpIntegral = 0.0f;
    pid->fpOutput = 0.0f;
    pid->fpRawOutput = 0.0f;

    // 默认反算抗饱和系数：若调用方未在静态初始化中指定（为 0），
    // 给出安全默认值，避免抗饱和失效导致积分器 windup 低频谱振
    if(pid->fpKaw == 0.0f) pid->fpKaw = 1.0f;
}


RAMFUNC void PID_SetParam(ST_PID *pid,
                  float Kp,
                  float Ki,
                  float Kd,
                  float Dt,
                  float Kaw)
{
    pid->fpKp = Kp;
    pid->fpKi = Ki;
    pid->fpKd = Kd;
    pid->fpDt = Dt;

    pid->fpKiDt = Ki * Dt;
    pid->fpKdDivDt = Kd / Dt;

    pid->fpKaw = Kaw;
}


RAMFUNC void PID_Reset(ST_PID *pid)
{
    pid->fpErr = 0.0f;
    pid->fpPreErr = 0.0f;

    pid->fpIntegral = 0.0f;
    pid->fpOutput = 0.0f;
    pid->fpRawOutput = 0.0f;
}


RAMFUNC float PID_Calc(ST_PID *pid, float err, float OutMin, float OutMax)
{
    float p_out;
    float i_out;
    float d_out;
    float raw_out;
    float sat_out;
    float aw_err;

    pid->fpPreErr = pid->fpErr;
    pid->fpErr = err;

    p_out = pid->fpKp * err;
    i_out = pid->fpIntegral + pid->fpKiDt * err;
    d_out = pid->fpKdDivDt * (err - pid->fpPreErr);
    
    raw_out = p_out + i_out + d_out;                // 未限幅输出
    
    sat_out = fminf(fmaxf(raw_out, OutMin), OutMax);   // 实际输出限幅

    /*
     * 反算法 / 回算抗饱和：    aw_err = sat_out - raw_out
     * 当没有饱和时：sat_out == raw_out   aw_err = 0  积分项正常累加
     * 当上限饱和时：sat_out < raw_out    aw_err < 0  积分项被往回拉
     * 当下限饱和时：sat_out > raw_out    aw_err > 0  积分项被往回拉
     */
    aw_err = sat_out - raw_out;

    pid->fpIntegral = i_out + pid->fpKaw * aw_err;

    pid->fpRawOutput = raw_out;
    pid->fpOutput = sat_out;

    return sat_out;
}

/*******************************************************************************
 * 比例-准谐振控制器（PR）计算函数
 * 抗饱和算法使用香港理工大学论文《用于逆变器的比例-谐振控制器的抗饱和方案》
 ******************************************************************************/
inline void PR_Control(ST_PR *pstPR)
{
    float PR_Limit = pstPR->fpLim;
    
    pstPR->fpE = (pstPR->pre_is_cut) ? 
                ((pstPR->output_fpU - pstPR->fpU) * pstPR->InvKp + pstPR->fpPreE) : 
                (pstPR->fpDes - pstPR->fpFB);
    
	pstPR->fpU =    pstPR->n0* pstPR->fpE+
                    pstPR->n1* pstPR->fpPreE+
                    pstPR->n2* pstPR->fpPre_PreE-
                    pstPR->d1* pstPR->pre_fpU-
                    pstPR->d2* pstPR->pre_pre_fpU;
	
	pstPR->fpPre_PreE = pstPR->fpPreE;//迭代结果
	pstPR->fpPreE = pstPR->fpE; //保存本次偏差
	
    pstPR->output_fpU = fminf(fmaxf(pstPR->fpU, -PR_Limit), PR_Limit);

    pstPR->pre_is_cut = (pstPR->fpU != pstPR->output_fpU);//限幅输出与原始输出不一致，说明发生了限幅
    
	pstPR->pre_pre_fpU = pstPR->pre_fpU;
	pstPR->pre_fpU = pstPR->fpU;
}

//计算PR算法离散参数/初始化
inline void PR_Init(ST_PR *pstPR)
{		
    pstPR->InvKp = 1/ pstPR->Kp;
    pstPR->n0=((4+4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt)*pstPR->Kp+4*pstPR->Kr*pstPR->fpDt*pstPR->omiga_c)/(4+4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt);
    pstPR->n1=pstPR->Kp*(2*pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt-8)/(4+4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt);
    pstPR->n2=((4-4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt)*pstPR->Kp-4*pstPR->Kr*pstPR->fpDt*pstPR->omiga_c)/(4+4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt);
    pstPR->d1=(2*pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt-8)/(4+4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt);
    pstPR->d2=(4-4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt)/(4+4*pstPR->fpDt*pstPR->omiga_c+pstPR->omiga_0*pstPR->omiga_0*pstPR->fpDt*pstPR->fpDt);
}

//重置PR控制器状态
inline void PR_Clean(ST_PR *pstPR)
{
	pstPR->fpE = 0;
	pstPR->fpPreE = 0;
	pstPR->fpPre_PreE = 0;
	pstPR->fpU = 0;
	pstPR->pre_fpU = 0;
	pstPR->pre_pre_fpU = 0;
    pstPR->output_fpU = 0;
    pstPR->pre_is_cut = 0;
}
	
//PR控制器结构体示例	
/*
RAMVAR ST_PR PR_A={.fpE=0,.fpPreE=0,.fpPre_PreE=0,.fpU=0,.pre_fpU=0,.pre_pre_fpU=0,
	.omiga_c=0.628f,.omiga_0=DEFAULT_AC_OMEGA,.fpDt=CTRL_DT,.Kp=0.1f,.Kr=500             
};		
RAMVAR ST_PR PR_B={.fpE=0,.fpPreE=0,.fpPre_PreE=0,.fpU=0,.pre_fpU=0,.pre_pre_fpU=0,
	.omiga_c=0.628f,.omiga_0=DEFAULT_AC_OMEGA,.fpDt=CTRL_DT,.Kp=0.1f,.Kr=500               
};*/

/*******************************************************************************
 * 比例-积分-准谐振控制器（PIR）计算函数
 * 待完善
 ******************************************************************************/

/*******************************************************************************
 * 比例-多重准谐振控制器（MPR）计算函数
 ******************************************************************************/
static inline void MPR_CalcResonantCoeff(ST_MPR_RESONANT *r, float Dt)
{
    float w0Dt;
    float w0Dt2;
    float den;
    float k;

    /* 图中形式：
     * Gr(s) = wc * s / (s^2 + wc * s + w0^2)
     *
     * 双线性变换后，b1 = 0：
     *
     * y[k] = b0 * e[k] + b2 * e[k-2]
     *        - d1 * y[k-1] - d2 * y[k-2]
     *
     * 总输出：
     * u = Kp * e + Kr * sum(Gr_i)
     */
    w0Dt  = r->w0 * Dt;
    w0Dt2 = w0Dt * w0Dt;

    den = 4.0f + 2.0f * Dt * r->wc + w0Dt2;

    k = 2.0f * Dt * r->wc / den;

    r->b0 =  k;
    r->b2 = -k;

    r->d1 = (2.0f * w0Dt2 - 8.0f) / den;
    r->d2 = (4.0f - 2.0f * Dt * r->wc + w0Dt2) / den;
}

void MPR_Build(ST_MPR *pstMPR)
{
    uint8_t i;

    if (pstMPR->fpLimMin > pstMPR->fpLimMax)
    {
        float temp = pstMPR->fpLimMin;
        pstMPR->fpLimMin = pstMPR->fpLimMax;
        pstMPR->fpLimMax = temp;
    }

    pstMPR->InvKp = (pstMPR->Kp > MPR_EPS || pstMPR->Kp < -MPR_EPS) ? (1.0f / pstMPR->Kp) : 0.0f;

    for (i = 0u; i < MPR_RESONANT_NUM; i++)
    {
        MPR_CalcResonantCoeff(&pstMPR->resonant[i], pstMPR->fpDt);
    }

    MPR_Reset(pstMPR);
}

void MPR_SetKp(ST_MPR *pstMPR, float Kp)
{
    pstMPR->Kp = Kp;
    pstMPR->InvKp = (Kp > MPR_EPS || Kp < -MPR_EPS) ? (1.0f / Kp) : 0.0f;
}

void MPR_SetKr(ST_MPR *pstMPR, float Kr)
{
    uint8_t i;
    for (i = 0u; i < MPR_RESONANT_NUM; i++)
        pstMPR->resonant[i].Kr = Kr;
}

void MPR_SetResonantKr(ST_MPR *pstMPR, uint8_t index, float Kr)
{
    if (index >= MPR_RESONANT_NUM) return;

    pstMPR->resonant[index].Kr = Kr;
}

inline void MPR_SetResonant(ST_MPR *pstMPR, uint8_t index, float omega_0, float omega_c)
{
    ST_MPR_RESONANT *r;

    if (index >= MPR_RESONANT_NUM) return;

    r = &pstMPR->resonant[index];

    r->w0 = omega_0;    // omega_0：谐振中心角频率，rad/s
    r->wc = omega_c;    // omega_c：准谐振带宽角频率，rad/s

    r->y  = 0.0f;
    r->y1 = 0.0f;
    r->y2 = 0.0f;

    MPR_CalcResonantCoeff(r, pstMPR->fpDt);
}

inline void MPR_SetLimit(ST_MPR *pstMPR, float LimMin, float LimMax)
{
    if (LimMin > LimMax)
    {
        float temp = LimMin;
        LimMin = LimMax;
        LimMax = temp;
    }

    pstMPR->fpLimMin = LimMin;
    pstMPR->fpLimMax = LimMax;
}


#define MPR_BRANCH_STEP(_mpr, _idx, _e0, _e2, _sum)                 \
    do                                                             \
    {                                                              \
        ST_MPR_RESONANT *r = &((_mpr)->resonant[(_idx)]);          \
        float y;                                                   \
                                                                   \
        y = r->b0 * (_e0)                                          \
          + r->b2 * (_e2)                                          \
          - r->d1 * r->y1                                          \
          - r->d2 * r->y2;                                         \
                                                                   \
        r->y2 = r->y1;                                             \
        r->y1 = y;                                                 \
        r->y  = y;                                                 \
                                                                   \
        (_sum) += r->Kr * y;                                       \
    } while (0)

static inline float MPR_ResonantSum_Unrolled(ST_MPR *pstMPR, float e0, float e2)
{
    float r_sum = 0.0f;

#if (MPR_RESONANT_NUM > 0u)
    MPR_BRANCH_STEP(pstMPR, 0u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 1u)
    MPR_BRANCH_STEP(pstMPR, 1u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 2u)
    MPR_BRANCH_STEP(pstMPR, 2u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 3u)
    MPR_BRANCH_STEP(pstMPR, 3u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 4u)
    MPR_BRANCH_STEP(pstMPR, 4u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 5u)
    MPR_BRANCH_STEP(pstMPR, 5u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 6u)
    MPR_BRANCH_STEP(pstMPR, 6u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 7u)
    MPR_BRANCH_STEP(pstMPR, 7u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 8u)
    MPR_BRANCH_STEP(pstMPR, 8u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 9u)
    MPR_BRANCH_STEP(pstMPR, 9u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 10u)
    MPR_BRANCH_STEP(pstMPR, 10u, e0, e2, r_sum);
#endif

#if (MPR_RESONANT_NUM > 11u)
    MPR_BRANCH_STEP(pstMPR, 11u, e0, e2, r_sum);
#endif

    return r_sum;
}

inline float MPR_Control(ST_MPR *pstMPR)
{
    float e_real;
    float e_use;
    float p_out;
    float r_sum;
    float raw_u;
    float sat_u;

    //真实误差
    e_real = pstMPR->fpDes - pstMPR->fpFB;

    /*
     * 全局抗饱和。
     *
     * 多个谐振支路共用一个抗饱和机制。
     * 不对每个谐振支路分别限幅。
     */
    if (pstMPR->pre_is_cut != 0u) e_use = pstMPR->fpPreE + (pstMPR->output_fpU - pstMPR->fpU) * pstMPR->InvKp;
    else e_use = e_real;

    pstMPR->fpE = e_use;

    r_sum = MPR_ResonantSum_Unrolled(pstMPR, e_use, pstMPR->fpPrePreE);

    raw_u = pstMPR->Kp * e_use + r_sum;

    sat_u = fminf(fmaxf(raw_u, pstMPR->fpLimMin), pstMPR->fpLimMax);

    pstMPR->pre_is_cut = (raw_u != sat_u) ? 1u : 0u;

    /*
     * 统一更新误差历史。
     */
    pstMPR->fpPrePreE = pstMPR->fpPreE;
    pstMPR->fpPreE = e_use;

    pstMPR->fpU = raw_u;
    pstMPR->output_fpU = sat_u;

    return sat_u;
}

inline void MPR_Reset(ST_MPR *pstMPR)
{
    uint8_t i;

    pstMPR->fpE = 0.0f;
    pstMPR->fpPreE = 0.0f;
    pstMPR->fpPrePreE = 0.0f;

    pstMPR->fpU = 0.0f;
    pstMPR->output_fpU = 0.0f;

    pstMPR->pre_is_cut = 0u;

    for (i = 0u; i < MPR_RESONANT_NUM; i++)
    {
        pstMPR->resonant[i].y = 0.0f;
        pstMPR->resonant[i].y1 = 0.0f;
        pstMPR->resonant[i].y2 = 0.0f;
    }
}


/*******************************************************************************
 * 二阶数字陷波器
 ******************************************************************************/
void Notch_Init(ST_NOTCH *h)
{
    float w0 = TWO_PI_F * h->f0 / h->fs;
    float sin_w0 = sinf(w0);
    float cos_w0 = cosf(w0);
    float alpha = sin_w0 / (2.0f * h->Q);
    float inv_a0 = 1.0f / (1.0f + alpha);

    h->b0 = inv_a0;
    h->b1 = -2.0f * cos_w0 * inv_a0;
    h->b2 = inv_a0;
    h->a1 = -2.0f * cos_w0 * inv_a0;
    h->a2 = (1.0f - alpha) * inv_a0;

    h->s1 = 0.0f;
    h->s2 = 0.0f;
}

//TDF-II 实时运算
inline float Notch_Calc(ST_NOTCH *h, float x)
{
    float y = h->b0 * x + h->s1;
    h->s1 = h->b1 * x - h->a1 * y + h->s2;
    h->s2 = h->b2 * x - h->a2 * y;
    return y;
}

//串联陷波器运算
RAMFUNC float NotchCascade_Calc(ST_NOTCH_CASCADE *h, float x)
{
    for (uint_fast8_t i = 0; i < h->num; i++)
        x = Notch_Calc(&h->stage[i], x);
    return x;
}

/*******************************************************************************
 * 二阶数字带通滤波器
 *
 * 与 Notch_Init() 使用相同极点，构造互补传递函数：
 *
 *     H_BP(z) = 1 - H_Notch(z)
 *
 * 因此中心频率处峰值为 1，即 0 dB。
 ******************************************************************************/
void BandPass_Init(ST_BANDPASS *h)
{
    float w0 = TWO_PI_F * h->f0 / h->fs;
    float sin_w0 = sinf(w0);
    float cos_w0 = cosf(w0);
    float alpha = sin_w0 / (2.0f * h->Q);
    float inv_a0 = 1.0f / (1.0f + alpha);

    h->b0 =  alpha * inv_a0;
    h->b1 =  0.0f;
    h->b2 = -alpha * inv_a0;
    h->a1 = -2.0f * cos_w0 * inv_a0;
    h->a2 = (1.0f - alpha) * inv_a0;

    h->s1 = 0.0f;
    h->s2 = 0.0f;
}

// TDF-II 实时运算
inline float BandPass_Calc(ST_BANDPASS *h, float x)
{
    float y = h->b0 * x + h->s1;
    h->s1 = h->b1 * x - h->a1 * y + h->s2;
    h->s2 = h->b2 * x - h->a2 * y;
    return y;
}