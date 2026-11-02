/* Low-overhead sequencer: notes store hardware ARR values (not Hz).
 * Only one note transition per coroutine pass; TIM14 generates PWM in hardware.
 * Delays are timestamp comparisons, never HAL_Delay or busy waiting.
 */
#include "framework/beep.h"
#include "framework/beep_hw.h"

static const beep_note_t s_key_sound[] = {
    {BEEP_ARR_HZ(2700u),100u}, BEEP_END
};
static const beep_note_t *s_queue[BEEP_QUEUE_CAPACITY];
static const beep_note_t *s_current;
static uint32_t s_step_started;
static uint8_t s_head,s_tail,s_count,s_index,s_key_enabled,s_muted;

void beep_init(void)
{
    s_head=s_tail=s_count=s_index=0u;
    s_current=0;s_step_started=0u;
    s_key_enabled=0u; /* Click Beep: default OFF */
    s_muted=0u;       /* Mute Beep : default OFF */
    beep_hw_init();
    beep_hw_stop();
}
uint8_t beep_enqueue(const beep_note_t *sequence)
{
    if(s_muted || !sequence || sequence[0].duration_ms==0u ||
       s_count>=BEEP_QUEUE_CAPACITY)return 0u;
    s_queue[s_head]=sequence;
    s_head=(uint8_t)((s_head+1u)%BEEP_QUEUE_CAPACITY);
    ++s_count;
    return 1u;
}
void beep_poll(uint32_t now_ms)
{
    if(s_current)
    {
        if((uint32_t)(now_ms-s_step_started)<
           s_current[s_index].duration_ms)return;
        ++s_index;
    }
    /* A clip may terminate just as the next clip becomes available.
     * Limit the loop even for malformed/empty sequence data. */
    for(uint8_t n=0u;n<=BEEP_QUEUE_CAPACITY;++n)
    {
        if(!s_current)
        {
            /* Output was muted when the preceding clip ended; avoid
             * touching TIM14 on every idle superloop iteration. */
            if(!s_count)return;
            s_current=s_queue[s_tail];
            s_tail=(uint8_t)((s_tail+1u)%BEEP_QUEUE_CAPACITY);
            --s_count;
            s_index=0u;
        }
        if(s_index>=BEEP_SEQUENCE_MAX_STEPS ||
           s_current[s_index].duration_ms==0u)
        {
            s_current=0;
            beep_hw_stop();
            continue;
        }
        beep_hw_set_arr(s_current[s_index].arr);
        s_step_started=now_ms;
        return;
    }
}
void beep_stop(void)
{
    s_head=s_tail=s_count=s_index=0u;
    s_current=0;
    beep_hw_stop();
}
uint8_t beep_busy(void){return (uint8_t)(s_current!=0 || s_count!=0u);}
uint8_t beep_queue_count(void){return s_count;}
uint8_t beep_note_index(void){return s_index;}
void beep_set_key_enabled(uint8_t enabled){s_key_enabled=enabled?1u:0u;}
uint8_t beep_get_key_enabled(void){return s_key_enabled;}
void beep_set_muted(uint8_t muted)
{
    muted=muted?1u:0u;
    if(s_muted==muted)return;
    s_muted=muted;
    if(s_muted)beep_stop();
}
uint8_t beep_is_muted(void){return s_muted;}
void beep_key_click(void)
{
    /* Don't flood the queue with auto-repeat or delay a popup melody. */
    if(s_key_enabled && !s_muted && !beep_busy())
        (void)beep_enqueue(s_key_sound);
}
uint8_t beep_popup(const beep_note_t *sequence)
{
    return beep_enqueue(sequence);
}
