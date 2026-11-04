#ifndef IH01A_HEATER_CLI_PARSE_H
#define IH01A_HEATER_CLI_PARSE_H
#include <stdint.h>
/* Exact, lower-case, whitespace-delimited command parser.
 * Non-heater lines return 0, so legacy PD/UART commands stay untouched.
 * Other heater lines return 1 with BAD_SYNTAX / OUT_OF_RANGE. */
typedef enum {
    HEATER_CLI_BAD_SYNTAX=0,
    HEATER_CLI_GET,
    HEATER_CLI_ON,
    HEATER_CLI_OFF,
    HEATER_CLI_PHASE,
    HEATER_CLI_AUTO_ON,
    HEATER_CLI_AUTO_OFF,
    HEATER_CLI_FREQ_DISABLED,
    HEATER_CLI_OUT_OF_RANGE
} HeaterCliOp;
typedef struct {
    HeaterCliOp op;
    uint16_t value;
} HeaterCliRequest;
uint8_t HeaterCli_Parse(const char *line,HeaterCliRequest *out);
#endif
