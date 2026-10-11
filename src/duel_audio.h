/* XWADuelVR native audio (P1.2): event-driven SFX + engine loops + music.
 *
 * Sources (user's own install, local use only, pushed to duel_assets/audio,
 * NEVER in the repo or APK): X-wing/TIE lasers, impacts, fighter
 * explosion, cockpit engine loops, alarm, R2, decoded XWA conflict music.
 *
 * Split: this header + duel_audio_core.c are SDL-FREE (unit-tested);
 * duel_audio.c holds the SDL3 device/stream/WAV glue. The sim is NEVER
 * touched: DuelAudio_PollEvents detects muzzle/hp/alive transitions.
 */
#ifndef XWADUELVR_AUDIO_H
#define XWADUELVR_AUDIO_H

#include <stdint.h>

#include "duel_math.h"
#include "duel_sim.h"

#define DUEL_AUDIO_RATE 44100
#define DUEL_AUDIO_VOICES 16
#define DUEL_AUDIO_CHUNK 1024 /* frames per mix slice */
/* F32 bus: mixer sums S16-scale floats; NormalizeChunk maps to +-1.0. */
#define DUEL_AUDIO_MASTER 0.8f
#define DUEL_AUDIO_LOOP_XFADE 2048 /* loop crossfade frames at load */

typedef enum DuelAudioEvent {
    AUDIO_PLAYER_FIRE = 0,
    AUDIO_TIE_FIRE,
    AUDIO_PLAYER_HIT,
    AUDIO_TIE_HIT,
    AUDIO_PLAYER_EXPLODE,
    AUDIO_TIE_EXPLODE,
    AUDIO_WARNING,
    AUDIO_R2,
    AUDIO__COUNT
} DuelAudioEvent;

/* Loaded sound: S16 PCM, native rate/channels (mixer resamples). */
typedef struct DuelSound {
    int16_t *pcm;    /* frames*channels samples, owned by SDL glue */
    unsigned frames; /* per channel */
    int rate;
    int channels;
} DuelSound;

typedef struct DuelVoice {
    const DuelSound *snd;
    double pos;        /* fractional frame position */
    double step;       /* src frames per output frame (rate ratio * pitch) */
    float lgain, rgain;
    int active;
    int loop;
} DuelVoice;

typedef struct DuelListener {
    DuelVec3 pos;
    DuelVec3 fwd;
    DuelVec3 right;
} DuelListener;

/* Mixer state (SDL-free). */
typedef struct DuelAudioState {
    DuelSound sounds[AUDIO__COUNT];
    DuelSound engine_player, engine_tie, music;
    DuelVoice voices[DUEL_AUDIO_VOICES];
    DuelVoice loop_engine_p, loop_engine_t, loop_music;
    /* transition tracking (M5: per-ship; allies share the X-wing
     * gun/explosion samples positionally, never the cockpit-local mix) */
    int last_muzzle_p, last_muzzle_t[DUEL_TIE_COUNT], last_muzzle_a[DUEL_ALLY_COUNT];
    float last_hp_p, last_hp_t[DUEL_TIE_COUNT], last_hp_a[DUEL_ALLY_COUNT];
    int last_alive_p, last_alive_t[DUEL_TIE_COUNT], last_alive_a[DUEL_ALLY_COUNT];
    int synced; /* M10: first poll snapshots silently (parked ships mute) */
    float warn_cd;
    int ready; /* set once sounds are loaded */
    /* M7 menu volumes (init = the frozen #define mix). */
    float vol_master, vol_music, vol_sfx;
} DuelAudioState;

/* ---- pure math (unit-tested) ---- */

/* Distance attenuation: 1 at 0, 0 at >= maxd, smooth 1/(1+x^2)-ish. */
static inline float duel_audio_atten(float dist, float maxd) {
    if (dist <= 0.0f) return 1.0f;
    if (dist >= maxd) return 0.0f;
    float x = dist / maxd;
    return (1.0f - x) * (1.0f - x);
}

/* Constant-power pan: x=-1 left .. +1 right. */
static inline void duel_audio_pan(float x, float *l, float *r) {
    float cx = duel_clampf(x, -1.0f, 1.0f);
    float a = (cx + 1.0f) * 0.78539816f; /* 0..pi/2 */
    *l = cosf(a);
    *r = sinf(a);
}

static inline int16_t duel_audio_clamp(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)v;
}

/* ---- SDL engine (duel_audio.c) ---- */
typedef struct DuelAudio DuelAudio;
DuelAudio *DuelAudio_Create(const char *audio_dir);
void DuelAudio_Update(DuelAudio *a, const DuelState *s, DuelListener lis,
                      float dt);
void DuelAudio_Destroy(DuelAudio *a);
/* M7 menu: live volumes (clamped 0..1; music default is the mix value). */
void DuelAudio_SetVolumes(DuelAudio *a, float master, float music, float sfx);

/* ---- core (duel_audio_core.c, SDL-free) ---- */
void DuelAudio_InitState(DuelAudioState *st);
void DuelAudio_PollEvents(DuelAudioState *st, const DuelState *s,
                          DuelListener lis, float dt);
/* Mix `frames` stereo float frames (appended to out, which must hold
 * frames*2 floats, zeroed by caller... actually ADDS into out). */
void DuelAudio_MixChunk(DuelAudioState *st, float *out, unsigned frames);
/* Map a mixed S16-scale chunk to F32 +-1.0 (master gain included). */
void DuelAudio_NormalizeChunk(float *out, unsigned frames, float master);
/* In-place loop crossfade: blends the tail into a copy of the head so a
 * loop wraps seamlessly (call once at load for loop sounds). */
void DuelAudio_LoopBlend(int16_t *pcm, unsigned frames, int ch, unsigned x);

#endif /* XWADUELVR_AUDIO_H */
