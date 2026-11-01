#include "algorithm_filtering.h"
#include <math.h>
#include <armdsp.h>

/*一阶低通滤波*/
//off_freq单位为rad/s
//1/(1+TS)  off_freq为1/T
inline void LpFilter(ST_LPF *lpf)
{
	float fir_a = 1 / (1 + lpf->off_freq * lpf->samp_tim);
	lpf->out = fir_a * lpf->preout + (1 - fir_a) * lpf->in;
	lpf->preout = lpf->out;
}

/*仪表显示专用滤波算法
 *既保证小变化下的稳定输出，也保证了快速变化时的即时响应
 */
float VarFilter(FilterVar_t *pFilter, float filter_input){
    //累计误差
    pFilter->integral_error += filter_input- pFilter->display_value;
    //定时更新
    pFilter->filter_ticks++;
    if(pFilter->filter_ticks > pFilter->update_ticks){
        pFilter->filter_ticks = 0;
        if(fabsf(pFilter->integral_error) > pFilter->UpdateThreshold){
            pFilter->display_value += pFilter->integral_error/ pFilter->update_ticks;
            pFilter->integral_error = 0;
        }
    }
    //变化量过大时立刻更新
    if(fabsf(pFilter->integral_error) > pFilter->UpdateThreshold* 20){
        pFilter->display_value = filter_input;
        pFilter->integral_error = 0;
    }
    
    return pFilter->display_value;
}

