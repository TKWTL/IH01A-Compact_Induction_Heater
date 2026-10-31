/* Four independent active-low keys, sampled every 10ms.
 * Debounce 60ms; L/R DAS 800ms, autorepeat every 200ms.
 * ENTR/EXIT act on release; UI wake consumes the initial press.
 */
#define IH_BTN_DEBOUNCE_TICKS 6u
#define IH_BTN_DAS_TICKS      80u
#define IH_BTN_REPEAT_TICKS   20u
#include "buttons.h"
#include "main.h"

typedef struct {
    uint8_t last_raw, stable_down, consecutive, suppressed;
    uint16_t held_ticks;
} button_state_t;
static button_state_t s_state[4];
static uint8_t s_actions, s_press, s_wake;
static const uint8_t s_bits[4]={IH_KEY_L,IH_KEY_R,IH_KEY_ENTR,IH_KEY_EXIT};

static uint8_t button_raw(uint8_t id)
{
    switch(id)
    {
    case 0: return LL_GPIO_IsInputPinSet(L_GPIO_Port,L_Pin) ? 0u : 1u;
    case 1: return LL_GPIO_IsInputPinSet(R_GPIO_Port,R_Pin) ? 0u : 1u;
    case 2: return LL_GPIO_IsInputPinSet(ENTR_GPIO_Port,ENTR_Pin) ? 0u : 1u;
    default:return LL_GPIO_IsInputPinSet(EXIT_GPIO_Port,EXIT_Pin) ? 0u : 1u;
    }
}
void Buttons_Init(void)
{
    uint8_t i;
    for(i=0u;i<4u;i++)
    {
        s_state[i].last_raw=button_raw(i);
        s_state[i].stable_down=s_state[i].last_raw;
        s_state[i].consecutive=IH_BTN_DEBOUNCE_TICKS;
        s_state[i].held_ticks=0u;
        s_state[i].suppressed=s_state[i].stable_down;
    }
    s_actions=0u;s_press=0u;s_wake=0u;
}
void Buttons_Poll10ms(void)
{
    uint8_t i;
    for(i=0u;i<4u;i++)
    {
        uint8_t raw=button_raw(i);
        button_state_t *b=&s_state[i];
        if(raw==b->last_raw)
        {
            if(b->consecutive<IH_BTN_DEBOUNCE_TICKS) b->consecutive++;
        }
        else
        {
            b->last_raw=raw;
            b->consecutive=1u;
        }
        if(b->consecutive>=IH_BTN_DEBOUNCE_TICKS && b->stable_down!=raw)
        {
            b->stable_down=raw;
            b->held_ticks=0u;
            if(raw)
            {
                s_press |= s_bits[i];
                s_wake=1u;
                if(i<2u && !b->suppressed) s_actions |= s_bits[i];
            }
            else
            {
                if(i>=2u && !b->suppressed) s_actions |= s_bits[i];
                b->suppressed=0u;
            }
        }
        if(b->stable_down && b->last_raw && i<2u && !b->suppressed)
        {
            if(b->held_ticks<65535u) b->held_ticks++;
            if(b->held_ticks>=IH_BTN_DAS_TICKS &&
               ((b->held_ticks-IH_BTN_DAS_TICKS)%IH_BTN_REPEAT_TICKS)==0u)
                s_actions |= s_bits[i];
        }
    }
}
uint8_t Buttons_TakeAction(void)
{
    uint8_t event;
    if(s_actions & IH_KEY_EXIT) event=IH_KEY_EXIT;
    else if(s_actions & IH_KEY_ENTR) event=IH_KEY_ENTR;
    else if(s_actions & IH_KEY_L) event=IH_KEY_L;
    else event=s_actions & IH_KEY_R;
    s_actions &= (uint8_t)~event;
    return event;
}
uint8_t Buttons_TakePress(void) { uint8_t p=s_press;s_press=0u;return p; }
uint8_t Buttons_TakeWake(void) { uint8_t p=s_wake;s_wake=0u;return p; }
void Buttons_ClearActions(void) { s_actions=0u;s_press=0u;s_wake=0u; }
void Buttons_SuppressHeld(void)
{
    uint8_t i;
    for(i=0u;i<4u;i++)
        if(s_state[i].stable_down) s_state[i].suppressed=1u;
    Buttons_ClearActions();
}
