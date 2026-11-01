#ifndef IH01A_DEBUG_UART_H
#define IH01A_DEBUG_UART_H
#include <stdint.h>
#define DEBUG_UART_TX_RING_SIZE 512u
#define DEBUG_UART_RX_RING_SIZE 512u
typedef struct {
    uint32_t tx_dropped, rx_dropped, rx_errors, rx_idle_events;
} DebugUART_Stats;
void DebugUART_Init(uint32_t reset_flags);
void DebugUART_Service(void);
void DebugUART_WriteByte(uint8_t byte);
uint32_t DebugUART_Write(const uint8_t *data, uint32_t size);
uint8_t DebugUART_ReadByte(uint8_t *byte);
void DebugUART_GetStats(DebugUART_Stats *out);
#endif
