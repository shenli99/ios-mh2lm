/* Private backend of SweetPmicKeys. The IOService owns its worker lifetime. */
#ifndef SWEET_KEYS_H
#define SWEET_KEYS_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool sweet_keys_prepare(void);
bool sweet_keys_start(void);
void sweet_keys_event(unsigned events);
void sweet_keys_stop(void);
#ifdef __cplusplus
}
#endif
#endif
