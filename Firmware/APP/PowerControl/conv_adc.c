#include "conv_adc.h"

extern DMA_HandleTypeDef hdma_adc1;
volatile uint16_t ADC1_Buffer[16];
volatile uint16_t ADC2_Buffer[16];
volatile uint16_t ADC3_Buffer[16];
volatile uint16_t ADC4_Buffer[16];

RAMRO const float ADC12B_COUNT_TO_VOLT_F32    = ADC_VREF/ 4096.0f;

//需控制的参数配置，更换新的一套采样参数时，可以留着旧的参数避免其他文件大量报错，在修改掉依赖文件后再删除
// ADC1: IN(ch0), VN(ch1), VBUS(ch2) → VN 已移至 PA1/ADC1_IN2
// ADC2: IC(ch0), VCM(ch1)
// ADC3: IB(ch0), VAB(ch1)
// ADC4: IA(ch0), VBC(ch1), VZS(ch2)
RAMVAR PowerControlVar_t 
    VBUS_t= {ADC1_Buffer, 2, 23.1285f, 0.10011f, 0},//母线电压，单位V
    IA_t  = {ADC4_Buffer, 0, 7.5778f, -12.4713f, 0},//逆变A相电流，单位A
    IB_t  = {ADC3_Buffer, 0, 7.6044f, -12.489f, 0}, //逆变B相电流，单位A
    IC_t  = {ADC2_Buffer, 0, 7.547f, -12.451f, 0}, //逆变C相电流，单位A
    //IC_t  = {ADC2_Buffer, 0, 7.547f, -12.391f, 0}, //逆变C相电流，单位A
    VAB_t = {ADC3_Buffer, 1, 39.951f, -66.105f, 0},//逆变A-B线电压，单位V
    VBC_t = {ADC4_Buffer, 1, 39.924f, -66.003f, 0},    //逆变B-C线电压，单位V
    VZS_t = {ADC4_Buffer, 2, 29.0059f, -48.2676f, 0}, //三相零序对地电压，单位V
    IN_t  = {ADC1_Buffer, 0, 7.6162f, -12.5366f, 0}, //整流器输入电流，单位A（极性：输入→半桥为负）
    VN_t  = {ADC1_Buffer, 1, 39.979f, -66.225f, 0}, //整流器输入电压，单位V（PA1→ADC1_IN2）
    VCM_t = {ADC2_Buffer, 1, 19.75f, 0.0f, 0};           //整流器共模电压，单位V
    //实际待采集的量->对应的单片机端口->对应的ADC名称与通道->存放转换结果的数组和索引号

//中间函数（未来会被展开）
static inline void CalcControlVar(PowerControlVar_t *p_var){
    float temp;
    uint16_t tmp = p_var->pBuffer[p_var->channel];
		
    temp = tmp* ADC12B_COUNT_TO_VOLT_F32;
		
    temp *= p_var->slope;
    temp += p_var->zero;
		
    p_var->result = temp;
}
//环路ISR一开始就必须执行的参数加载函数（将放入CCM RAM中加速运行）
RAMFUNC inline void LoadControlVar(){
    CalcControlVar(&IN_t);
    CalcControlVar(&VN_t);
    //CalcControlVar(&VCM_t);
    CalcControlVar(&VBUS_t);
    CalcControlVar(&IA_t);
    //CalcControlVar(&IB_t);
    CalcControlVar(&IC_t);
    CalcControlVar(&VAB_t);
    CalcControlVar(&VBC_t);
    CalcControlVar(&VZS_t);
}

//从结构体中配置好的参数中取得数值并以float格式输出
inline float ReadControlVar(PowerControlVar_t *p_var){
    return p_var->result;
}


//启动ADC的DMA转换，需要在上电后调用一次
//每个ADC有几个Number Of Conversion，就在后面填几个
void ADC_Init(void){
    //ADC校准：每次上电后必须执行，需在ADC使能之前、Init之后调用
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc4, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc5, ADC_SINGLE_ENDED);
    
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)ADC1_Buffer, 3);
    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_TC);  //关闭TC中断，使用HT触发MainLoop
    HAL_ADC_Start_DMA(&hadc2, (uint32_t*)ADC2_Buffer, 2);
    HAL_ADC_Start_DMA(&hadc3, (uint32_t*)ADC3_Buffer, 2);
    HAL_ADC_Start_DMA(&hadc4, (uint32_t*)ADC4_Buffer, 3);

    LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_1);
    LL_DAC_Enable(DAC1, LL_DAC_CHANNEL_2);
    LL_DAC_Enable(DAC2, LL_DAC_CHANNEL_1);
}

/******************************自动校准函数************************************/
//作为菜单项，需要使用串口与和外界的自动校准系统协同工作，进行参数的自动校准
//依赖usart.c提供的USART收发API
//在ui_conf.c中注册：AddItem("-Auto Calibration", UI_ITEM_ONCE_FUNCTION, img_scan, &I2C_Scanner_Item, &Tool_Page, NULL, i2c_Scanner);
void AutoCalibration(ui_t *ui){
    //
}

/***********************以下函数须在ui_conf.c中调用****************************/
//ui_item_t LV_Slope_Item, LV_Offset_Item, HV_Slope_Item, HV_Offset_Item, IL_Slope_Item, IL_Offset_Item;
//创建参数补偿模块所需的参数
void Create_ADC_Parameters(ui_t *ui){
    /*static ui_data_t LV_Slope_data;
    LV_Slope_data.name = "VL Slope";
    LV_Slope_data.ptr = &LV1_t.slope;
    LV_Slope_data.dataType = UI_DATA_FLOAT;
    LV_Slope_data.actionType = UI_DATA_ACTION_RW;
    LV_Slope_data.max = 12;
    LV_Slope_data.min = 10;
    LV_Slope_data.step = 0.01f;
    static ui_element_t LV_Slope_element;
    LV_Slope_element.data = &LV_Slope_data;
    Create_element(&LV_Slope_Item, &LV_Slope_element);
    
    static ui_data_t LV_Offset_data;
    LV_Offset_data.name = "VL Offset";
    LV_Offset_data.ptr = &LV1_t.zero;
    LV_Offset_data.dataType = UI_DATA_FLOAT;
    LV_Offset_data.actionType = UI_DATA_ACTION_RW;
    LV_Offset_data.max = 1;
    LV_Offset_data.min = -1;
    LV_Offset_data.step = 0.01f;
    static ui_element_t LV_Offset_element;
    LV_Offset_element.data = &LV_Offset_data;
    Create_element(&LV_Offset_Item, &LV_Offset_element);
    
    static ui_data_t HV_Slope_data;
    HV_Slope_data.name = "VH Slope";
    HV_Slope_data.ptr = &HV_t.slope;
    HV_Slope_data.dataType = UI_DATA_FLOAT;
    HV_Slope_data.actionType = UI_DATA_ACTION_RW;
    HV_Slope_data.max = 17;
    HV_Slope_data.min = 15;
    HV_Slope_data.step = 0.01f;
    static ui_element_t HV_Slope_element;
    HV_Slope_element.data = &HV_Slope_data;
    Create_element(&HV_Slope_Item, &HV_Slope_element);
    
    static ui_data_t HV_Offset_data;
    HV_Offset_data.name = "VL Offset";
    HV_Offset_data.ptr = &HV_t.zero;
    HV_Offset_data.dataType = UI_DATA_FLOAT;
    HV_Offset_data.actionType = UI_DATA_ACTION_RW;
    HV_Offset_data.max = 1;
    HV_Offset_data.min = -1;
    HV_Offset_data.step = 0.01f;
    static ui_element_t HV_Offset_element;
    HV_Offset_element.data = &HV_Offset_data;
    Create_element(&HV_Offset_Item, &HV_Offset_element);
    
    static ui_data_t IL_Slope_data;
    IL_Slope_data.name = "IL Slope";
    IL_Slope_data.ptr = &IL1_t.slope;
    IL_Slope_data.dataType = UI_DATA_FLOAT;
    IL_Slope_data.actionType = UI_DATA_ACTION_RW;
    IL_Slope_data.max = 5;
    IL_Slope_data.min = 3;
    IL_Slope_data.step = 0.01f;
    static ui_element_t IL_Slope_element;
    IL_Slope_element.data = &IL_Slope_data;
    Create_element(&IL_Slope_Item, &IL_Slope_element);
    
    static ui_data_t IL_Offset_data;
    IL_Offset_data.name = "IL Offset";
    IL_Offset_data.ptr = &IL1_t.zero;
    IL_Offset_data.dataType = UI_DATA_FLOAT;
    IL_Offset_data.actionType = UI_DATA_ACTION_RW;
    IL_Offset_data.max = 1;
    IL_Offset_data.min = -1;
    IL_Offset_data.step = 0.001f;
    static ui_element_t IL_Offset_element;
    IL_Offset_element.data = &IL_Offset_data;
    Create_element(&IL_Offset_Item, &IL_Offset_element);*/
}
//将参数补偿模块的对象添加到菜单中
void Add_ADC_Items(ui_page_t *ParentPage){
    /*AddItem(" VL Slope", UI_ITEM_DATA, NULL, &LV_Slope_Item, ParentPage, NULL, NULL);
    AddItem(" VL Offset", UI_ITEM_DATA, NULL, &LV_Offset_Item, ParentPage, NULL, NULL);
    AddItem(" VH Slope", UI_ITEM_DATA, NULL, &HV_Slope_Item, ParentPage, NULL, NULL);
    AddItem(" VH Offset", UI_ITEM_DATA, NULL, &HV_Offset_Item, ParentPage, NULL, NULL);
    AddItem(" IL Slope", UI_ITEM_DATA, NULL, &IL_Slope_Item, ParentPage, NULL, NULL);
    AddItem(" IL Offset", UI_ITEM_DATA, NULL, &IL_Offset_Item, ParentPage, NULL, NULL);*/
}
