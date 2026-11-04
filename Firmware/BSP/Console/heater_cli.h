#ifndef IH01A_HEATER_CLI_H
#define IH01A_HEATER_CLI_H
#include <stdint.h>
/* Processes only the strict "heater ..." command namespace. Returns 1 if
 * handled (including syntax/range errors), 0 for legacy PD/UART commands. */
uint8_t HeaterCLI_Dispatch(const char *line);
#endif
