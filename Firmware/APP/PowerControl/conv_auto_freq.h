#ifndef IH01A_CONV_AUTO_FREQ_H
#define IH01A_CONV_AUTO_FREQ_H
#include <stdint.h>

/* Provisional 12V bench tracker. 128MHz TIM1 PSC=0:
 * 300kHz => ARR=426; 170kHz => ARR=752.
 * Loaded ZVS has NOT yet been oscilloscope-qualified. */
#define CONV_AF_START_KHZ       300u
#define CONV_AF_MIN_KHZ         170u
#define CONV_AF_START_ARR       426u
#define CONV_AF_MIN_ARR         752u
#define CONV_AF_TARGET_DECI    200   /* +20.0deg current lag */
#define CONV_AF_DEADBAND_DECI   20    /* +/-2.0deg */
#define CONV_AF_LOCK_RAW       175u  /* IBUS ~0.50A over calibrated zero */
#define CONV_AF_RELEASE_RAW    140u  /* hysteresis ~0.40A */
#define CONV_AF_INTERVAL_MS      5u /* telemetry only; ISR uses frame count */
#define CONV_AF_FRAMES_PER_UPDATE 100u /* 20kHz ADC DMA => fixed 5ms */

typedef enum {
    CONV_AF_OFF=0, CONV_AF_SAFE, CONV_AF_ACQUIRE,
    CONV_AF_TRACK, CONV_AF_RECOVER, CONV_AF_FAULT
} ConvAutoFreqState;

typedef struct {
    uint8_t enabled, output_on, hardware_ok, phase_fit;
    uint16_t arr, ibus_delta_raw;
    int16_t lag_corr_deci_deg;
} ConvAutoFreqInput;
typedef struct {
    uint16_t requested_arr;
    ConvAutoFreqState state;
    uint8_t trip;
} ConvAutoFreqDecision;
typedef struct {
    ConvAutoFreqState state;
    uint8_t ibus_locked, good_samples, bad_samples;
} ConvAutoFreqCore;
typedef struct {
    ConvAutoFreqState state;
    uint16_t ibus_delta_raw, requested_arr;
    int16_t lag_corr_deci_deg;
    uint32_t last_ms;
    uint8_t enabled, ibus_locked, phase_fit;
} ConvAutoFreqSnapshot;

/* Pure, deterministic 5ms state machine; host-tested without HAL. */
void ConvAutoFreq_CoreReset(ConvAutoFreqCore *core);
void ConvAutoFreq_CoreStep(ConvAutoFreqCore *core,
                           const ConvAutoFreqInput *input,
                           ConvAutoFreqDecision *decision);

/* ISR path updates the 5ms loop every 100 complete ADC frames.
 * The ENABLE setting is foreground-owned; mode changes stop PC6 before
 * returning to 300kHz and must be followed by a fresh explicit RUN. */
void ConvAutoFreq_Init(void);
uint8_t ConvAutoFreq_SetEnabled(uint8_t enabled);
uint8_t ConvAutoFreq_Enabled(void);
void ConvAutoFreq_FrameISR(const volatile uint16_t *frame);
/* Q12 phase quantization, no division in ISR, returns 0 out of ARR band. */
uint8_t ConvAutoFreq_CorrectLagFast(uint16_t arr,uint16_t ccr3,
                                    uint16_t ic2,int16_t *lag_deci);
void ConvAutoFreq_GetSnapshot(ConvAutoFreqSnapshot *out);
const char *ConvAutoFreq_StateName(ConvAutoFreqState state);
#endif
