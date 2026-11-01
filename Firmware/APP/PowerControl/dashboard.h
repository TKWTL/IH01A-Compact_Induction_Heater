#ifndef __DASHBOARD_H__
#define __DASHBOARD_H__

#ifdef __cplusplus
extern "C" {
#endif
    
#include "services.h"

/* 仪表盘行 Y 坐标 */
#define DASH_Y_ROW1     23    // Vo1 / Io
#define DASH_Y_ROW2     36    // Vo2 / VBUS
#define DASH_Y_ROW3     49    // fo / VN
#define DASH_Y_ROW4     62    // PF / 留空

typedef struct {
    float *pvalue;  //要修改的变量指针
    float step;     //步进值
    float max;      //上限值
    float min;      //下限值
    float *pmaxlim; //上限变量指针，不用时初始化为NULL
    float *pminlim; //下限变量指针，不用时初始化为NULL
}SetVar_t;//参数设置结构体定义

extern float VO_Set, VO_Set2, fo_Set, PF_Set;
extern SetVar_t SetVar[];

void DashBoard(ui_t *ui);//供ui_conf调用的显示函数

static inline void VarInc(uint8_t index){
    if(SetVar[index].pvalue == NULL) return;
    *SetVar[index].pvalue += SetVar[index].step;
    if(*SetVar[index].pvalue > SetVar[index].max) *SetVar[index].pvalue = SetVar[index].max;
    if(SetVar[index].pmaxlim != NULL && *SetVar[index].pvalue > *SetVar[index].pmaxlim) *SetVar[index].pvalue = *SetVar[index].pmaxlim;
}

static inline void VarDec(uint8_t index){
    if(SetVar[index].pvalue == NULL) return;
    *SetVar[index].pvalue -= SetVar[index].step;
    if(*SetVar[index].pvalue < SetVar[index].min) *SetVar[index].pvalue = SetVar[index].min;
    if(SetVar[index].pminlim != NULL && *SetVar[index].pvalue < *SetVar[index].pminlim) *SetVar[index].pvalue = *SetVar[index].pminlim;
}

#ifdef __cplusplus
}
#endif

#endif
