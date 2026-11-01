/*显示仪表盘*/
#include "dashboard.h"
#include "algorithm_filtering.h"
#include "application.h"
#include <stdio.h>


//需要设置的变量 和对应结构体（左侧设定值）
RAMVAR float VO_Set  = 32.0f;              //输出线电压设定，单位V
RAMVAR float VO_Set2 = 32.0f;              //输出线电压设定2，单位V
RAMVAR float fo_Set  = DEFAULT_AC_FREQ;    //输出频率设定，单位Hz
RAMVAR float PF_Set  = 1.00f;              //输入功率因数设定

SetVar_t SetVar[] = {
    {&VO_Set,  0.02f,  34.0f,  30.0f,   NULL,   NULL},//设定输出线电压1
    {&VO_Set2, 0.02f,  34.0f,  30.0f,   NULL,   NULL},//设定输出线电压2
    {&fo_Set,  10.0f,  100.0f, 20.0f,   NULL,   NULL},//设定输出频率
    {&PF_Set,  0.01f,  1.00f,  0.80f,   NULL,   NULL},//设定输入功率因数
};//{&(Name),  Step,   Max,    Min,     MaxLim, MinLim}

//滤波器参数结构体定义及参数设置
FilterVar_t 
    VBUS_Filter = {0.2f, 50},
    VN_Filter   = {0.2f, 50},
    IOUT_Filter = {0.2f, 50},
    MISC_Filter = {0.2f, 50};

const char title[] = " ACAC-2026A ";
    
//供ui_conf调用的显示函数
void DashBoard(ui_t *ui){
    char buf[20];
    uint8_t color;
    int16_t value = 128, value2;
    static uint8_t var_switch = 0;//菜单选中指示，退出时保留
    
    while(1){
        //两种颜色下的清屏
        Disp_SetDrawColor(&ui->bgColor);
        Disp_DrawBox(0, 0, UI_HOR_RES, UI_VER_RES);
        color = ui->bgColor ? 0 : 1;
        Disp_SetDrawColor(&color);
        //入场动画效果
        value = (int16_t)UI_Animation(68, (float)value, &ui->animation.optionbarPos_ani);
        int16_t left  = 69 - value;
        int16_t right = value + 1;
        //标题
        Disp_SetFont(font_menu_main_h12w6);
        Disp_DrawStr(left, 10, title);
            
        //===== 左侧：设定值 =====
        //Vo Set 1
        sprintf(buf, "Voa :%2.2fV", VO_Set);
        Disp_DrawStr(left, DASH_Y_ROW1, buf);
        //Vo Set 2
        sprintf(buf, "Voc :%2.2fV", VO_Set2);
        Disp_DrawStr(left, DASH_Y_ROW2, buf);
        //fo Set
        sprintf(buf, "fout:%3.0fHz", fo_Set);
        Disp_DrawStr(left, DASH_Y_ROW3, buf);
        //PF Set
        sprintf(buf, "  PF: %1.2f", PF_Set);
        Disp_DrawStr(left, DASH_Y_ROW4, buf);
        
        //===== 右侧：显示值 =====
        //输出电流有效值（A/C相取大）
        sprintf(buf, "Iout:%1.2fA", VarFilter(&IOUT_Filter, fmaxf(IA_rms, IC_rms)));
        Disp_DrawStr(right, DASH_Y_ROW1, buf);
        //VBUS（陷波后）
        sprintf(buf, "BUS:%.2fV", VarFilter(&VBUS_Filter, Vbus_notched));
        Disp_DrawStr(right, DASH_Y_ROW2, buf);
        //输入电压有效值
        sprintf(buf, "Vin:%.2fV", VarFilter(&VN_Filter, Vin_rms));
        Disp_DrawStr(right, DASH_Y_ROW3, buf);
        //预留
        sprintf(buf, "IA:%.2f", VarFilter(&MISC_Filter, ReadControlVar(&IA_t)));
        //Disp_DrawStr(right, DASH_Y_ROW4, buf);
        
        Disp_SetFont(font_home_h6w4);
        sprintf(buf, "%02d:%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
        Disp_DrawStr(value+ 24, 6, buf);//时间
        sprintf(buf, "20%02d/%02d/%02d", sTime.Year, sTime.Month, sTime.Date);
        Disp_DrawStr(value+ 20, 12, buf);//日期
        
        
        color = 2;
        Disp_SetDrawColor(&color);
        //启动标识框
        if(g_conv_state == CONV_RUN || g_conv_state == CONV_PEND){
            Disp_DrawRBox(0, 0, UI_FONT_WIDTH* (sizeof(title)/sizeof(char)- 1)+ 1, UI_FONT_HIGHT- 1, 0);
        }
        //参数选择框
        value2 = (int16_t)UI_Animation(13* var_switch+ 14, (float)value2, &ui->animation.cursor_ani);
        Disp_DrawRBox(0, value2, 67, 10, 0);
        //横线
        color = ui->bgColor ? 0 : 1;
        Disp_SetDrawColor(&color);
        Disp_DrawLine(0, 13, UI_HOR_RES, 13);
        //Disp_DrawLine(0, 51, UI_HOR_RES, 51);
        
        //发送到屏幕
        Disp_SendBuffer();
        
        //按键控制
        switch(indevScan()){
            case UI_ACTION_BACK:
                ui->action = UI_ACTION_ENTER;
                return;
            case UI_ACTION_ENTER:
                if(g_conv_state == CONV_STOP) g_conv_state = CONV_PEND;   //先进入等待过零点
                else g_conv_state = CONV_STOP;
                break;
            case UI_ACTION_PLUS:
                if(var_switch == 0) var_switch = sizeof(SetVar)/sizeof(SetVar[0])-1;
                else var_switch--;
                break;
            case UI_ACTION_MINUS:
                if(var_switch >= sizeof(SetVar)/sizeof(SetVar[0])-1) var_switch = 0;
                else var_switch++;
                break;
            case UI_ACTION_UP:
                VarDec(var_switch);
                break;
            case UI_ACTION_DOWN:
                VarInc(var_switch);
                break;
            default:
                break;
        }
        
        osDelay(pdMS_TO_TICKS(20));
    }
}
