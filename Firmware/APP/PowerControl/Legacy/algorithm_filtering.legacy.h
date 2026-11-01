#ifndef __ALGORITHM_FILTERING_H__
#define __ALGORITHM_FILTERING_H__

#ifdef __cplusplus
extern "C" {
#endif    

#include <math.h>
#include <armdsp.h>
#include <stdint.h>
    
typedef struct
{
	float preout;
	float out;
	float in;
	float off_freq;
	float samp_tim;
}ST_LPF;

extern void LpFilter(ST_LPF *lpf);


typedef struct {
    //用于设置，需要初始化
    float UpdateThreshold;
    int update_ticks;
    //内部变量
    float display_value;
    float integral_error;
    int filter_ticks;
}FilterVar_t;//仪表显示用滤波算法的参数结构体

float VarFilter(FilterVar_t *pFilter, float filter_input);

/*******************************************************************************
 * 滑动窗口有效值计算句柄
 * 维护一个环形缓冲区，使用平方和累加器实现 O(1) 更新
 ******************************************************************************/
typedef struct {
    float *buf;         // 环形缓冲区指针（外部提供存储）
    uint16_t len;       // 缓冲区长度
    uint16_t ptr;       // 当前写入位置
    float sum_sq;       // 平方和累加器
    float rms;          // 当前有效值
} SlideRms_t;

static inline void SlideRms_Init(SlideRms_t *p, float *buf, uint16_t len)
{
    p->buf    = buf;
    p->len    = len;
    p->ptr    = 0;
    p->sum_sq = 0.0f;
    p->rms    = 0.0f;
}
static inline void SlideRms_Store(SlideRms_t *p, float val)
{
    p->sum_sq -= p->buf[p->ptr] * p->buf[p->ptr];
    p->buf[p->ptr] = val;
    p->sum_sq += val * val;
    p->ptr++;
    if(p->ptr >= p->len) p->ptr = 0;
}
static inline void SlideRms_Update(SlideRms_t *p)
{
    p->rms = sqrtf(p->sum_sq * (1.0f / (float)p->len));
}
static inline float SlideRms_ReadDelayed(const SlideRms_t *p, uint16_t delay)
{
    int16_t idx = (int16_t)p->ptr - 1 - (int16_t)delay;
    while(idx < 0) idx += (int16_t)p->len;
    return p->buf[idx];
}

#ifdef __cplusplus
}
#endif

#endif // __FILTERING_ALGORITHM_H__
