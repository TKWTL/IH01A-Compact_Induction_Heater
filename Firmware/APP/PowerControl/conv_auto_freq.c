/* IH01A frequency-only phase loop: phase command controls power, while
 * frequency keeps the CT-detected current on the inductive side.
 * The current CT_ZERO delay regression is EMPTY-COIL only. This is an
 * experimental 12V bench controller, NOT a validated ZVS interlock.
 * Low current holds 300kHz; no blind frequency descent is permitted. */
#include "conv_auto_freq.h"
#ifndef CONV_AUTO_FREQ_HOST_TEST
#include "conv_controller.h"
#include "conv_adc.h"
#include "conv_protection.h"
#include "heater_control.h"
#include "tim.h"
#include "main.h"
#endif

static ConvAutoFreqCore s_core;
static ConvAutoFreqSnapshot s_snapshot;
static uint8_t s_enabled;
static volatile uint8_t s_frame_div;
/* At most 1.3kB of Flash: validated empty-coil correction in quarter-
 * TIM1 ticks and the Q12 factor (900/(ARR+1)) for tenths of degrees.
 * This avoids all software division inside the 5ms DMA ISR on Cortex-M0+.
 * LUT generation: same three-segment integer formula as conv_zcd.c;
 * rounding to 0.25 tick adds <=0.2deg quantization at 300kHz.
 * Entries ARR=426..752, inclusive. NEVER extrapolate outside this band. */
static const int16_t s_delay_q4[327]={

    232, 232, 231, 231, 231, 230, 230, 230, 230, 229, 229, 229, 
    228, 228, 228, 228, 227, 227, 227, 227, 226, 226, 226, 225, 
    225, 225, 225, 224, 224, 224, 224, 223, 223, 223, 223, 222, 
    222, 222, 222, 221, 221, 221, 221, 220, 220, 220, 220, 219, 
    219, 219, 219, 218, 218, 218, 218, 218, 217, 217, 217, 217, 
    216, 216, 216, 216, 215, 215, 215, 215, 215, 214, 214, 214, 
    214, 214, 213, 213, 213, 213, 212, 212, 212, 212, 212, 211, 
    211, 211, 211, 211, 210, 210, 210, 210, 210, 209, 209, 209, 
    209, 209, 208, 208, 208, 208, 208, 207, 207, 207, 207, 207, 
    206, 206, 206, 206, 206, 206, 205, 205, 205, 205, 205, 204, 
    204, 204, 204, 204, 204, 203, 203, 203, 203, 203, 202, 201, 
    201, 200, 199, 198, 197, 196, 196, 195, 194, 193, 192, 191, 
    191, 190, 189, 188, 187, 187, 186, 185, 184, 184, 183, 182, 
    181, 180, 180, 179, 178, 177, 177, 176, 175, 174, 174, 173, 
    172, 171, 171, 170, 169, 168, 168, 167, 166, 166, 165, 164, 
    163, 163, 162, 161, 161, 160, 159, 159, 158, 157, 156, 156, 
    155, 154, 154, 153, 152, 152, 151, 150, 150, 149, 148, 148, 
    147, 146, 146, 145, 144, 144, 143, 142, 142, 141, 141, 140, 
    139, 139, 138, 137, 137, 136, 135, 135, 134, 134, 133, 132, 
    132, 131, 131, 130, 129, 129, 128, 128, 127, 126, 126, 125, 
    125, 124, 123, 123, 122, 122, 121, 121, 120, 119, 119, 118, 
    118, 117, 117, 116, 115, 115, 114, 114, 113, 113, 112, 111, 
    111, 110, 110, 109, 109, 108, 108, 107, 107, 106, 106, 105, 
    104, 104, 103, 103, 102, 102, 101, 101, 100, 100, 99, 99, 
    98, 98, 97, 97, 96, 96, 95, 95, 94, 94, 93, 93, 
    92, 92, 91, 91, 90, 89, 86, 84, 81, 79, 76, 74, 
    72, 69, 67, 64, 62, 60, 57, 55, 52, 50, 48, 45, 
    43, 41, 38
};
static const uint16_t s_deg_mul_q12[327]={

    8633, 8613, 8593, 8573, 8553, 8533, 8514, 8494, 8474, 8455, 8436, 8416, 
    8397, 8378, 8359, 8340, 8321, 8303, 8284, 8265, 8247, 8229, 8210, 8192, 
    8174, 8156, 8138, 8120, 8102, 8084, 8067, 8049, 8031, 8014, 7997, 7979, 
    7962, 7945, 7928, 7911, 7894, 7877, 7860, 7843, 7827, 7810, 7794, 7777, 
    7761, 7745, 7728, 7712, 7696, 7680, 7664, 7648, 7632, 7617, 7601, 7585, 
    7570, 7554, 7539, 7523, 7508, 7493, 7477, 7462, 7447, 7432, 7417, 7402, 
    7388, 7373, 7358, 7343, 7329, 7314, 7300, 7285, 7271, 7257, 7242, 7228, 
    7214, 7200, 7186, 7172, 7158, 7144, 7130, 7117, 7103, 7089, 7076, 7062, 
    7049, 7035, 7022, 7008, 6995, 6982, 6969, 6955, 6942, 6929, 6916, 6903, 
    6890, 6878, 6865, 6852, 6839, 6827, 6814, 6801, 6789, 6776, 6764, 6752, 
    6739, 6727, 6715, 6703, 6690, 6678, 6666, 6654, 6642, 6630, 6618, 6606, 
    6595, 6583, 6571, 6559, 6548, 6536, 6525, 6513, 6502, 6490, 6479, 6467, 
    6456, 6445, 6434, 6422, 6411, 6400, 6389, 6378, 6367, 6356, 6345, 6334, 
    6323, 6312, 6302, 6291, 6280, 6269, 6259, 6248, 6238, 6227, 6217, 6206, 
    6196, 6185, 6175, 6165, 6154, 6144, 6134, 6124, 6113, 6103, 6093, 6083, 
    6073, 6063, 6053, 6043, 6033, 6024, 6014, 6004, 5994, 5984, 5975, 5965, 
    5955, 5946, 5936, 5927, 5917, 5908, 5898, 5889, 5879, 5870, 5861, 5851, 
    5842, 5833, 5824, 5815, 5805, 5796, 5787, 5778, 5769, 5760, 5751, 5742, 
    5733, 5724, 5715, 5707, 5698, 5689, 5680, 5671, 5663, 5654, 5645, 5637, 
    5628, 5620, 5611, 5602, 5594, 5585, 5577, 5569, 5560, 5552, 5543, 5535, 
    5527, 5519, 5510, 5502, 5494, 5486, 5478, 5469, 5461, 5453, 5445, 5437, 
    5429, 5421, 5413, 5405, 5397, 5389, 5382, 5374, 5366, 5358, 5350, 5343, 
    5335, 5327, 5319, 5312, 5304, 5297, 5289, 5281, 5274, 5266, 5259, 5251, 
    5244, 5236, 5229, 5222, 5214, 5207, 5199, 5192, 5185, 5178, 5170, 5163, 
    5156, 5149, 5141, 5134, 5127, 5120, 5113, 5106, 5099, 5092, 5085, 5078, 
    5071, 5064, 5057, 5050, 5043, 5036, 5029, 5022, 5016, 5009, 5002, 4995, 
    4988, 4982, 4975, 4968, 4962, 4955, 4948, 4942, 4935, 4928, 4922, 4915, 
    4909, 4902, 4896
};
uint8_t ConvAutoFreq_CorrectLagFast(uint16_t arr,uint16_t ccr3,
                                    uint16_t ic2,int16_t *out)
{
    int32_t delta4,period4,scaled;
    uint16_t offset;
    if(!out || arr<CONV_AF_START_ARR || arr>CONV_AF_MIN_ARR ||
       ccr3>arr/2u || ic2>arr)return 0u;
    offset=(uint16_t)(arr-CONV_AF_START_ARR);
    period4=4*((int32_t)arr+1);
    delta4=4*(int32_t)ic2-(int32_t)arr-2*(int32_t)ccr3-
           s_delay_q4[offset];
    if(delta4>period4/2)delta4-=period4;
    else if(delta4< -period4/2)delta4+=period4;
    scaled=delta4*(int32_t)s_deg_mul_q12[offset];
    if(scaled>=0)scaled+=2048;
    else scaled-=2048;
    *out=(int16_t)(scaled/4096); /* C99 truncates towards zero */
    return 1u;
}

void ConvAutoFreq_CoreReset(ConvAutoFreqCore *core)
{
    if(!core)return;
    core->state=CONV_AF_OFF;
    core->ibus_locked=0u;
    core->good_samples=0u;
    core->bad_samples=0u;
}
void ConvAutoFreq_CoreStep(ConvAutoFreqCore *core,
                           const ConvAutoFreqInput *in,
                           ConvAutoFreqDecision *out)
{
    if(!core || !in || !out)return;
    out->requested_arr=in->arr;
    out->trip=0u;
    if(!in->enabled)
    {
        ConvAutoFreq_CoreReset(core);
        out->state=CONV_AF_OFF;
        return;
    }
    if(!in->output_on)
    {
        ConvAutoFreq_CoreReset(core);
        core->state=CONV_AF_SAFE;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    /* ADC zero, COMP, BREAK, and phase >=120deg are prerequisites. */
    if(!in->hardware_ok)
    {
        core->state=CONV_AF_FAULT;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        out->trip=1u;
        return;
    }
    if(!core->ibus_locked && in->ibus_delta_raw>=CONV_AF_LOCK_RAW)
        core->ibus_locked=1u;
    else if(core->ibus_locked &&
            in->ibus_delta_raw<=CONV_AF_RELEASE_RAW)
        core->ibus_locked=0u;
    if(!core->ibus_locked)
    {
        core->state=CONV_AF_ACQUIRE;
        core->good_samples=0u;
        core->bad_samples=0u;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    /* No fitted current-zero edge => high-frequency recovery.
     * Persistent missing feedback at nontrivial input current trips after
     * 20*5ms=100ms. Never substitute the raw, uncompensated angle. */
    if(!in->phase_fit)
    {
        core->state=CONV_AF_RECOVER;
        core->good_samples=0u;
        if(core->bad_samples<20u)++core->bad_samples;
        if(core->bad_samples>=20u)
        {
            core->state=CONV_AF_FAULT;
            out->trip=1u;
        }
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    /* Zero or negative lag is capacitive/ambiguous: withdraw PC6 first.
     * +0..+5deg is too close to the boundary: recover to 300kHz,
     * and trip if it persists for 6 samples. */
    if(in->lag_corr_deci_deg<=0)
    {
        core->state=CONV_AF_FAULT;
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        out->trip=1u;
        return;
    }
    if(in->lag_corr_deci_deg<50)
    {
        core->state=CONV_AF_RECOVER;
        core->good_samples=0u;
        if(core->bad_samples<6u)++core->bad_samples;
        if(core->bad_samples>=6u)
        {
            core->state=CONV_AF_FAULT;
            out->trip=1u;
        }
        out->requested_arr=CONV_AF_START_ARR;
        out->state=core->state;
        return;
    }
    core->bad_samples=0u;
    if(core->good_samples<3u)++core->good_samples;
    if(core->good_samples<3u)
    {
        core->state=CONV_AF_ACQUIRE;
        out->state=core->state;
        /* Enter the loop without a sudden downward step. */
        return;
    }
    core->state=CONV_AF_TRACK;
    out->state=core->state;
    if(in->arr<CONV_AF_START_ARR || in->arr>CONV_AF_MIN_ARR)
    {
        out->requested_arr=CONV_AF_START_ARR;
        core->state=CONV_AF_RECOVER;
        out->state=core->state;
        return;
    }
    /* Q0 actuator with 0.5 ARR count/degree over a fixed 5ms window.
     * e is in 0.1deg, so round(e/20), saturating to [-8,+4].
     * Frequency rises quickly on insufficient inductive lag; falls
     * more slowly towards the resonance from the safe high-frequency side.
     * Integral gain is deliberately ZERO until loaded-ZVS is validated. */
    {
        int32_t err=(int32_t)in->lag_corr_deci_deg-CONV_AF_TARGET_DECI;
        int32_t step;
        int32_t next;
        if(err>=-(int32_t)CONV_AF_DEADBAND_DECI &&
           err<=(int32_t)CONV_AF_DEADBAND_DECI)return;
        step=err>=0?(err+10)/20:(err-10)/20;
        if(step>4)step=4;
        if(step< -8)step=-8;
        next=(int32_t)in->arr+step;
        if(next<CONV_AF_START_ARR)next=CONV_AF_START_ARR;
        if(next>CONV_AF_MIN_ARR)next=CONV_AF_MIN_ARR;
        out->requested_arr=(uint16_t)next;
    }
}
const char *ConvAutoFreq_StateName(ConvAutoFreqState state)
{
    switch(state)
    {
    case CONV_AF_OFF: return "OFF";
    case CONV_AF_SAFE: return "SAFE";
    case CONV_AF_ACQUIRE: return "ACQUIRE";
    case CONV_AF_TRACK: return "TRACK";
    case CONV_AF_RECOVER: return "RECOVER";
    case CONV_AF_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}
void ConvAutoFreq_Init(void)
{
    ConvAutoFreq_CoreReset(&s_core);
    s_enabled=0u; /* explicit operator opt-in; regression not yet validated */
    s_snapshot.enabled=0u;
    s_snapshot.state=CONV_AF_OFF;
    s_snapshot.ibus_locked=0u;
    s_snapshot.phase_fit=0u;
    s_snapshot.ibus_delta_raw=0u;
    s_snapshot.requested_arr=CONV_AF_START_ARR;
    s_snapshot.lag_corr_deci_deg=0;
    s_snapshot.last_ms=0u;
    s_frame_div=0u;
}
uint8_t ConvAutoFreq_SetEnabled(uint8_t enabled)
{
#ifndef CONV_AUTO_FREQ_HOST_TEST
    uint32_t mask;
#endif
    enabled=(uint8_t)(enabled!=0u);
    if(enabled==s_enabled)return 1u;
#ifndef CONV_AUTO_FREQ_HOST_TEST
    /* Stop the ISR loop BEFORE staging 300kHz. Otherwise an ADC frame
     * could apply a new ARR after the foreground's manual off request.
     * Any change of tracking mode requires a fresh explicit RUN. */
    mask=__get_PRIMASK();
    __disable_irq();
    s_enabled=0u;
    s_frame_div=0u;
    ConvAutoFreq_CoreReset(&s_core);
    if((mask&1u)==0u)__enable_irq();
    if(ConvController_OutputEnabled())
    {
        ConvController_BenchPowerOff();
        Heater_Stop();
    }
    if(!ConvController_SetFrequencyArr(CONV_AF_START_ARR))
    {
        s_snapshot.enabled=0u;
        s_snapshot.state=CONV_AF_FAULT;
        return 0u;
    }
    mask=__get_PRIMASK();
    __disable_irq();
#endif
    ConvAutoFreq_CoreReset(&s_core);
    s_frame_div=0u;
    s_snapshot.state=enabled?CONV_AF_SAFE:CONV_AF_OFF;
    s_snapshot.enabled=enabled;
    s_enabled=enabled; /* commit enable LAST */
#ifndef CONV_AUTO_FREQ_HOST_TEST
    if((mask&1u)==0u)__enable_irq();
#endif
    return 1u;
}
uint8_t ConvAutoFreq_Enabled(void){return s_enabled;}
void ConvAutoFreq_GetSnapshot(ConvAutoFreqSnapshot *out)
{
#ifndef CONV_AUTO_FREQ_HOST_TEST
    uint32_t mask;
#endif
    if(!out)return;
#ifndef CONV_AUTO_FREQ_HOST_TEST
    mask=__get_PRIMASK();
    __disable_irq();
#endif
    *out=s_snapshot;
#ifndef CONV_AUTO_FREQ_HOST_TEST
    if((mask&1u)==0u)__enable_irq();
#endif
}
/* ADC DMA callback at 20k complete frames/s.
 * First 99 IRQs: a single frame divider branch. Exactly on the 100th
 * frame (5ms), all feedback and preloaded timer writes run in this ISR.
 * HAL, printf, heater policy and any 64-bit math are prohibited here.
 * The exceptional ISR work must be scope-timed on the target; not yet
 * proven within the 50us ADC period / prior 4.5us ISR baseline. */
void ConvAutoFreq_FrameISR(const volatile uint16_t *frame)
{
#ifndef CONV_AUTO_FREQ_HOST_TEST
    ConvProtectionSnapshot protection;
    ConvAutoFreqInput in;
    ConvAutoFreqDecision action;
    uint16_t current,ct,raw_ic2,raw_ct,phase;
    uint32_t capture_flags;
    int16_t lag=0;
    uint8_t valid=0u;

    if(!s_enabled || !frame)return;
    if(++s_frame_div<CONV_AF_FRAMES_PER_UPDATE)return;
    s_frame_div=0u;
    ConvProtection_GetSnapshot(&protection);
    current=ConvADC_FilteredChannelISR(CONV_ADC_IBUS);
    ct=ConvADC_FilteredChannelISR(CONV_ADC_CT_RMS);
    raw_ct=frame[CONV_ADC_CT_RMS];
    in.enabled=1u;
    in.arr=(uint16_t)TIM1->ARR;
    in.output_on=ConvController_OutputEnabled();
    in.ibus_delta_raw=0u;
    if(protection.zero_status==CONV_ZERO_READY &&
       current>protection.ibus_zero_raw)
        in.ibus_delta_raw=(uint16_t)(current-protection.ibus_zero_raw);
    phase=ConvController_GetPhaseDegrees();
    in.hardware_ok=(uint8_t)(
        protection.zero_status==CONV_ZERO_READY &&
        protection.fault_mask==0u &&
        ConvController_GetState()==CONV_STATE_ADC_MONITOR &&
        ConvController_LogicPwmRunning() &&
        frame[CONV_ADC_IBUS]<4090u &&
        phase>=120u && phase<=180u &&
        (TIM1->SR&TIM_SR_BIF)==0u);
    /* CC2IF is observed even though 200kHz edges cause overcapture.
     * No aged result is reused, and low/saturated CT disables feedback.
     * 50mA corresponds to ceil(0.25253V/A*.05A/2.5V*4095)=21
     * post-zero ADC counts. */
    capture_flags=TIM1->SR;
    raw_ic2=(uint16_t)TIM1->CCR2;
    if((capture_flags&TIM_SR_CC2OF)!=0u)
        TIM1->SR=(uint32_t)~TIM_SR_CC2OF;
    valid=(uint8_t)(
        in.output_on && in.hardware_ok &&
        (capture_flags&TIM_SR_CC2IF)!=0u &&
        raw_ct<4090u &&
        ct>=(uint16_t)(protection.ct_zero_raw+21u) &&
        ConvAutoFreq_CorrectLagFast(in.arr,(uint16_t)TIM1->CCR3,
                                    raw_ic2,&lag));
    in.phase_fit=valid;
    in.lag_corr_deci_deg=lag;
    ConvAutoFreq_CoreStep(&s_core,&in,&action);
    s_snapshot.state=action.state;
    s_snapshot.enabled=s_enabled;
    s_snapshot.ibus_locked=s_core.ibus_locked;
    s_snapshot.phase_fit=valid;
    s_snapshot.ibus_delta_raw=in.ibus_delta_raw;
    s_snapshot.lag_corr_deci_deg=valid?lag:0;
    s_snapshot.requested_arr=action.requested_arr;
    s_snapshot.last_ms=HAL_GetTick();

    if(action.trip)
    {
        ConvProtection_AutoFreqTripISR();
        return;
    }
    if(action.requested_arr!=in.arr &&
       !ConvController_SetFrequencyArr(action.requested_arr))
    {
        s_snapshot.state=CONV_AF_FAULT;
        ConvProtection_AutoFreqTripISR();
    }
#else
    (void)frame;
#endif
}
