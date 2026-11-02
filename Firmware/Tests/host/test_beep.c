#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "framework/beep.h"
static uint16_t s_arr;
static uint32_t s_changes;
void beep_hw_init(void){s_arr=0;s_changes=0;}
void beep_hw_set_arr(uint16_t arr){s_arr=arr;++s_changes;}
void beep_hw_stop(void){s_arr=0;}
int main(void)
{
    static const beep_note_t melody[]={
        {BEEP_A4,100u},{BEEP_REST,50u},{BEEP_C5,200u},BEEP_END
    };
    assert(BEEP_ARR_HZ(2700u)==369u);
    beep_init();
    assert(beep_enqueue(melody)==1u);
    beep_poll(1000u);
    assert(s_arr==BEEP_A4 && beep_note_index()==0u);
    beep_poll(1099u);
    assert(s_arr==BEEP_A4 && s_changes==1u);
    beep_poll(1100u);
    assert(s_arr==0u && beep_note_index()==1u);
    beep_poll(1150u);
    assert(s_arr==BEEP_C5 && beep_note_index()==2u);
    beep_poll(1350u);
    assert(s_arr==0u && !beep_busy());
    assert(!beep_enqueue(0));
    beep_set_key_enabled(0u);beep_key_click();
    assert(!beep_busy());
    beep_set_key_enabled(1u);beep_key_click();
    assert(beep_busy());
    beep_poll(2000u);
    assert(s_arr==BEEP_ARR_HZ(2700u));
    beep_poll(2100u);
    assert(!beep_busy() && s_arr==0u);
    /* FIFO, overflow, stop, duration=0 termination and playback index. */
    for(unsigned i=0;i<BEEP_QUEUE_CAPACITY;i++)assert(beep_enqueue(melody));
    assert(!beep_enqueue(melody));
    assert(beep_queue_count()==BEEP_QUEUE_CAPACITY);
    beep_stop();
    assert(!beep_busy() && s_arr==0u);
    puts("beep: PASS");
    return 0;
}
