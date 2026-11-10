/* IH01A display-only bilingual text lookup; original English strings are keys. */
#ifndef IH_I18N_H
#define IH_I18N_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
const char *IH_I18n_Text(const char *english);
const uint8_t *IH_I18n_Font(void);
void IH_I18n_SetChinese(uint8_t enable);
uint8_t IH_I18n_IsChinese(void);
#ifdef __cplusplus
}
#endif
#endif
