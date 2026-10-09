/* XWADuelVR input: OpenXR actions (oculus touch bindings, mirroring
 * XWAQuest M8 input_xr.c) + M8 TouchConvert-equivalent mapping:
 * yaw=lx, pitch=-ly, roll=rx, throttle=ry lever, fire=RT hysteresis. */
#ifndef XWADUELVR_INPUT_H
#define XWADUELVR_INPUT_H

#include <SDL3/SDL.h>
#include <openxr/openxr.h>

#include "duel_sim.h"

int DuelInput_Init(XrInstance instance, XrSession session);
/* Polls actions into DuelInput. Returns 0 if session not focused (neutral). */
int DuelInput_Poll(DuelInput *out, float dt, XrTime display_time);
void DuelInput_Shutdown(void);

#endif /* XWADUELVR_INPUT_H */
