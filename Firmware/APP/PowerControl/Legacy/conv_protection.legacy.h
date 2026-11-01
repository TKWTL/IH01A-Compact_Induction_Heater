#ifndef __CONV_PROTECTION_H__
#define __CONV_PROTECTION_H__

#include "services.h"

void Protection(void);
void HRTIM_FLT_IRQHandler(void);
void FLT1_Monitor(void);

void Protection_Init1(ui_t *ui);//初始化保护参数计算
void Protection_Init2(ui_t *ui);//初始化保护参数计算

/***********************以下函数须在ui_conf.c中调用****************************/
//创建保护设置模块所需的参数
void Create_Protection_Parameters1(ui_t *ui);
void Create_Protection_Parameters2(ui_t *ui);
//创建保护选项设置页面
void Add_Protection_Page(ui_page_t *ParentPage);

#endif
