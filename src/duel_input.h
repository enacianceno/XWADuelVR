/* XWADuelVR input: OpenXR actions (oculus touch bindings, mirroring
 * XWAQuest M8 input_xr.c) + M36 handed stick mapping (default DIESTRA:
 * yaw=rx, pitch=-ry, roll=lx, throttle=ly lever; ZURDA mirrors) +
 * A/B confirm/back + aim poses. Either trigger fires. */
#ifndef XWADUELVR_INPUT_H
#define XWADUELVR_INPUT_H

#include <SDL3/SDL.h>
#include <openxr/openxr.h>

#include "duel_sim.h"

int DuelInput_Init(XrInstance instance, XrSession session);
/* M36 handedness: 0 right-handed (default), 1 left-handed (mirror). */
void DuelInput_SetLeftHanded(int left);
/* Polls actions into DuelInput. Returns 0 if session not focused (neutral). */
int DuelInput_Poll(DuelInput *out, float dt, XrTime display_time);
void DuelInput_Shutdown(void);

#endif /* XWADUELVR_INPUT_H */
