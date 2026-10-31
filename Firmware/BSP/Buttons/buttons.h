#ifndef IH_BUTTONS_H
#define IH_BUTTONS_H
#include <stdint.h>
/* Low-active GPIO buttons from CubeMX main.h. */
#define IH_KEY_L    0x01u
#define IH_KEY_R    0x02u
#define IH_KEY_ENTR 0x04u
#define IH_KEY_EXIT 0x08u
void Buttons_Init(void);
void Buttons_Poll10ms(void);
uint8_t Buttons_TakeAction(void);
uint8_t Buttons_TakePress(void);
uint8_t Buttons_TakeWake(void);
void Buttons_ClearActions(void);
void Buttons_SuppressHeld(void);
#endif
