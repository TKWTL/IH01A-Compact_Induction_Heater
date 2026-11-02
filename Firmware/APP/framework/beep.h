#ifndef IH01A_FRAMEWORK_BEEP_H
#define IH01A_FRAMEWORK_BEEP_H
#include <stdint.h>

/* TIM14 clock = 64 MHz, prescaler = 63 -> timer tick = 1 MHz.
 * Compute ARR in constant expressions when defining a sound, not at runtime:
 * e.g. BEEP_ARR_HZ(2700u) == 369 (approximately 2703 Hz).
 * A 0 ARR encodes a timed rest; duration_ms == 0 ends the sequence.
 * No division occurs when advancing between notes.
 */
#define BEEP_TICK_HZ        1000000u
#define BEEP_ARR_HZ(hz)    ((uint16_t)((BEEP_TICK_HZ+((hz)/2u))/(hz)-1u))
#define BEEP_REST          0u
#define BEEP_END           {0u,0u}
#define BEEP_QUEUE_CAPACITY 8u
#define BEEP_SEQUENCE_MAX_STEPS 64u

/* Rounded equal-temperament note names, translated to ARR at compile time.
 * Custom frequency constants may use BEEP_ARR_HZ(hz) directly. */
#define BEEP_C4  BEEP_ARR_HZ(262u)
#define BEEP_CS4 BEEP_ARR_HZ(277u)
#define BEEP_D4  BEEP_ARR_HZ(294u)
#define BEEP_DS4 BEEP_ARR_HZ(311u)
#define BEEP_E4  BEEP_ARR_HZ(330u)
#define BEEP_F4  BEEP_ARR_HZ(349u)
#define BEEP_FS4 BEEP_ARR_HZ(370u)
#define BEEP_G4  BEEP_ARR_HZ(392u)
#define BEEP_GS4 BEEP_ARR_HZ(415u)
#define BEEP_A4  BEEP_ARR_HZ(440u)
#define BEEP_AS4 BEEP_ARR_HZ(466u)
#define BEEP_B4  BEEP_ARR_HZ(494u)
#define BEEP_C5  BEEP_ARR_HZ(523u)
#define BEEP_CS5 BEEP_ARR_HZ(554u)
#define BEEP_D5  BEEP_ARR_HZ(587u)
#define BEEP_DS5 BEEP_ARR_HZ(622u)
#define BEEP_E5  BEEP_ARR_HZ(659u)
#define BEEP_F5  BEEP_ARR_HZ(698u)
#define BEEP_FS5 BEEP_ARR_HZ(740u)
#define BEEP_G5  BEEP_ARR_HZ(784u)
#define BEEP_GS5 BEEP_ARR_HZ(831u)
#define BEEP_A5  BEEP_ARR_HZ(880u)
#define BEEP_AS5 BEEP_ARR_HZ(932u)
#define BEEP_B5  BEEP_ARR_HZ(988u)
#define BEEP_C6  BEEP_ARR_HZ(1047u)

typedef struct {
    uint16_t arr;          /* TIM14 ARR register value, 0 = rest */
    uint16_t duration_ms;  /* 0 = end of sequence, no playback */
} beep_note_t;

/* Sequence pointers are NOT copied: provide static const / ROM arrays.
 * Enqueue from foreground only; coroOS is cooperative, not thread preemptive. */
void beep_init(void);
uint8_t beep_enqueue(const beep_note_t *sequence); /* 1 accepted, 0 full/invalid */
void beep_poll(uint32_t now_ms);
void beep_stop(void);
uint8_t beep_busy(void);
uint8_t beep_queue_count(void);
uint8_t beep_note_index(void);        /* 0-based index of current note */
void beep_set_key_enabled(uint8_t enabled);
uint8_t beep_get_key_enabled(void);
/* Independent global mute. Enabling immediately stops output and clears
 * queued audio; callers can unmute without changing the Click Beep setting. */
void beep_set_muted(uint8_t muted);
uint8_t beep_is_muted(void);
void beep_key_click(void);            /* opt-in/out 100ms at ~2.7kHz */
uint8_t beep_popup(const beep_note_t *sequence);

#endif
