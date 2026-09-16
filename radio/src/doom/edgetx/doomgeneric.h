#ifndef DOOM_GENERIC
#define DOOM_GENERIC

// #include <stdlib.h>
#include <stdint.h>
#include "debug.h"

#if defined(__cplusplus)
extern "C" {
#endif

// Full deflection of the gimbal axes reported through AD_RV and AD_RH, and so
// of the ev_joystick events made from them. Defined here rather than next to
// the event in d_event.h because that header cannot be included alongside
// EdgeTX's, which has its own conflicting event_t.
#define JOYAXIS_MAX 32767

void dg_Create();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
int DG_GetStrafe(void);
void button_update_loop();

#define DOOM_LOG debugPrintf

#if defined(__cplusplus)
}
#endif

#endif //DOOM_GENERIC
