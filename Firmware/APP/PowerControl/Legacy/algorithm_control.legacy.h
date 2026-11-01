#ifndef __ALGORITHM_CONTROL_H__
#define __ALGORITHM_CONTROL_H__

#include <stdint.h>
#include "math.h"
#include "main.h"

/*******************************************************************************
 * PID（比例-积分-微分）控制器
 ******************************************************************************/
//PID控制器结构体
typedef struct
{
    float fpKp;
    float fpKi;
    float fpKd;
    float fpDt;

    float fpKiDt;       // Ki * Dt，减少实时计算
    float fpKdDivDt;    // Kd / Dt，减少实时计算

    float fpKaw;        // 回算抗饱和系数，建议 0.05 ~ 1.0

    float fpErr;
    float fpPreErr;

    float fpIntegral;   // 积分项，单位直接等于输出单位
    float fpOutput;     // 实际限幅后的输出
    float fpRawOutput;  // 未限幅输出，便于调试
} ST_PID;

//PID控制器函数声明
void PID_Init(ST_PID *pid);                                                     // 初始化PID控制器
void PID_SetParam(ST_PID *pid, float Kp, float Ki, float Kd, float Dt, float Kaw); // 设置PID参数
void PID_Reset(ST_PID *pid);                                                    // 重置PID控制器
float PID_Calc(ST_PID *pid, float err, float OutMin, float OutMax);             // 执行PID控制算法

//PR控制器结构体
typedef struct
{
	float fpDes;                 //控制变量目标值
	float fpFB;	                //控制变量反馈值
	float fpLim;	            //抗饱和时外界输入的增益值，用于标幺化

	float fpE;	                //本次偏差
	float fpPreE;                //上次偏差
	float fpPre_PreE;            //上上次偏差
	
	float fpU;	                //本次PID运算结果
    float pre_fpU;               //上次的输出
	float pre_pre_fpU;           //上上次的输出
    
    float output_fpU;            //经过限幅后的输出
    int pre_is_cut;             //上一次限幅情况

	float omiga_0;               //中心频率
    float omiga_c;               //频带宽度    
	float Kp;                    //比例系数
	float Kr;                    //谐振系数
	float fpDt;                  //控制周期
	
    float InvKp;                 //比例系数的倒数
    float n0;
	float n1;
	float n2;
	float d1;
	float d2;
} ST_PR;

void PR_Control(ST_PR *pstPid);                                                 //QPR控制器，带抗饱和
void PR_Init(ST_PR *PR);                                                        //计算PR算法离散参数
void PR_Clean(ST_PR *PR);                                                       //重置PR控制器状态


/*******************************************************************************
 * 比例-多谐振（MPR）控制器
 ******************************************************************************/
#define MPR_RESONANT_NUM    6u      // 固定谐振支路数量：1/3/5/7/9/11次
#define MPR_EPS             1.0e-6f

typedef struct
{
    float wc;       // 准谐振带宽，rad/s
    float w0;       // 中心角频率，rad/s
    float Kr;       // 本支路谐振增益

    float b0;
    float b2;
    float d1;
    float d2;

    float y;
    float y1;
    float y2;
} ST_MPR_RESONANT;


typedef struct
{
    float Kp;       // 比例增益
    float InvKp;
    float fpDt;

    float fpDes;
    float fpFB;

    float fpE;
    float fpPreE;
    float fpPrePreE;

    float fpU;          // 未限幅总输出
    float output_fpU;   // 限幅后实际输出

    float fpLimMin;
    float fpLimMax;

    uint8_t pre_is_cut;

    ST_MPR_RESONANT resonant[MPR_RESONANT_NUM];
} ST_MPR;

void MPR_Build(ST_MPR *pstMPR);                                                         // 根据句柄内已有参数计算离散系数
void MPR_SetKp(ST_MPR *pstMPR, float Kp);                                               // 设置MPR控制器Kp参数
void MPR_SetKr(ST_MPR *pstMPR, float Kr);                                               // 设置所有支路Kr为同一值
void MPR_SetResonantKr(ST_MPR *pstMPR, uint8_t index, float Kr);                        // 设置单个支路Kr
void MPR_SetResonant(ST_MPR *pstMPR, uint8_t index, float omega_0, float omega_c);      // 设置谐振频率和带宽
void MPR_SetLimit(ST_MPR *pstMPR, float LimMin, float LimMax);                          // 设置MPR控制器限幅参数
float MPR_Control(ST_MPR *pstMPR);                                                      // 执行MPR控制算法
void MPR_Reset(ST_MPR *pstMPR);                                                         // 重置MPR控制器状态

/*******************************************************************************
 * 二阶数字陷波器（Transposed Direct Form II）
 ******************************************************************************/
#define TWO_PI_F  6.283185307f

typedef struct {
    float fs;       // 采样率 (Hz)
    float f0;       // 陷波中心频率 (Hz)
    float Q;        // 品质因数

    float b0, b1, b2;   // 分子系数
    float a1, a2;       // 分母系数（a0 = 1 归一化）

    float s1, s2;       // TDF-II 状态
} ST_NOTCH;

typedef struct {
    ST_NOTCH *stage;
    uint8_t num;
} ST_NOTCH_CASCADE;

void Notch_Init(ST_NOTCH *h);                                                   // 初始化陷波器
float Notch_Calc(ST_NOTCH *h, float x);                                         // 计算陷波器输出
float NotchCascade_Calc(ST_NOTCH_CASCADE *h, float x);                          // 计算级联陷波器输出

/*******************************************************************************
 * 二阶数字带通滤波器（Transposed Direct Form II，中心频率峰值 0 dB）
 ******************************************************************************/
typedef struct {
    float fs;       // 采样率 (Hz)
    float f0;       // 带通中心频率 (Hz)
    float Q;        // 品质因数

    float b0, b1, b2;   // 分子系数
    float a1, a2;       // 分母系数（a0 = 1 归一化）

    float s1, s2;       // TDF-II 状态
} ST_BANDPASS;

void BandPass_Init(ST_BANDPASS *h);                                             // 初始化带通滤波器
float BandPass_Calc(ST_BANDPASS *h, float x);                                   // 计算带通滤波器输出

#endif