/* XWADuelVR audio SDL glue: WAV load + device/stream + per-frame pump.
 * Degrades safely: missing files or no device => silence, never a crash.
 */
#include "duel_audio.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct DuelAudio {
    DuelAudioState st;
    SDL_AudioDeviceID dev;
    SDL_AudioStream *stream;
    float *mixbuf;
    Uint8 *wavmem[AUDIO__COUNT + 3]; /* owned buffers (sfx + engP/engT/mus) */
};

static const char *WAV_NAMES[AUDIO__COUNT + 3] = {
    "fire_player.wav", "fire_tie.wav", "hit_player.wav", "hit_tie.wav",
    "boom_player.wav", "boom_tie.wav", "alarm.wav", "r2.wav",
    "engine_xw.wav", "engine_tie.wav", "music.wav"
};

static int load_wav(const char *path, DuelSound *out, Uint8 **mem) {
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &spec, &buf, &len) || !buf || len == 0) {
        SDL_Log("DUEL audio: missing %s", path);
        return 0;
    }
    if (spec.format != SDL_AUDIO_S16) {
        SDL_Log("DUEL audio: %s not S16 (fmt 0x%x), skipped", path,
                (unsigned)spec.format);
        SDL_free(buf);
        return 0;
    }
    if (spec.channels < 1 || spec.channels > 2 || spec.freq < 8000) {
        SDL_Log("DUEL audio: %s bad spec, skipped", path);
        SDL_free(buf);
        return 0;
    }
    out->pcm = (int16_t *)buf;
    out->frames = len / ((unsigned)spec.channels * 2u);
    out->rate = spec.freq;
    out->channels = spec.channels;
    *mem = buf;
    return 1;
}

DuelAudio *DuelAudio_Create(const char *audio_dir) {
    DuelAudio *a = (DuelAudio *)calloc(1, sizeof(*a));
    int loaded = 0;
    size_t bytes = 0;
    if (!a) return NULL;
    DuelAudio_InitState(&a->st);
    for (int i = 0; i < AUDIO__COUNT; i++) {
        char path[600];
        snprintf(path, sizeof(path), "%s/%s", audio_dir, WAV_NAMES[i]);
        if (load_wav(path, &a->st.sounds[i], &a->wavmem[i])) {
            loaded++;
            bytes += (size_t)a->st.sounds[i].frames *
                     (size_t)a->st.sounds[i].channels * 2u;
        }
    }
    {
        DuelSound *extra[3] = { &a->st.engine_player, &a->st.engine_tie,
                                &a->st.music };
        for (int i = 0; i < 3; i++) {
            char path[600];
            snprintf(path, sizeof(path), "%s/%s", audio_dir,
                     WAV_NAMES[AUDIO__COUNT + i]);
            if (load_wav(path, extra[i], &a->wavmem[AUDIO__COUNT + i])) {
                loaded++;
                bytes += (size_t)extra[i]->frames *
                         (size_t)extra[i]->channels * 2u;
                DuelAudio_LoopBlend(extra[i]->pcm, extra[i]->frames,
                                    extra[i]->channels,
                                    DUEL_AUDIO_LOOP_XFADE);
            }
        }
    }
    a->st.ready = (loaded > 0);
    a->mixbuf = (float *)malloc(DUEL_AUDIO_CHUNK * 2 * sizeof(float));
    if (!a->mixbuf) {
        DuelAudio_Destroy(a);
        return NULL;
    }
    {
        SDL_AudioSpec want;
        SDL_zero(want);
        want.format = SDL_AUDIO_F32;
        want.channels = 2;
        want.freq = DUEL_AUDIO_RATE;
        a->dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
        if (a->dev) {
            a->stream = SDL_CreateAudioStream(&want, &want);
            if (a->stream && SDL_BindAudioStream(a->dev, a->stream))
                SDL_ResumeAudioDevice(a->dev);
            else {
                if (a->stream) {
                    SDL_DestroyAudioStream(a->stream);
                    a->stream = NULL;
                }
            }
        } else {
            SDL_Log("DUEL audio: no device (%s)", SDL_GetError());
        }
    }
    SDL_Log("DUEL_AUDIO device=%s sounds=%d/11 bytes=%u music=%s",
            a->dev ? "ok" : "NONE", loaded, (unsigned)bytes,
            a->st.music.pcm ? "ok" : "NONE");
    return a;
}

void DuelAudio_Update(DuelAudio *a, const DuelState *s, DuelListener lis,
                      float dt) {
    if (!a) return;
    DuelAudio_PollEvents(&a->st, s, lis, dt);
    if (!a->stream) return;
    /* Keep ~2 chunks queued; mix from silence each slice. */
    for (int guard = 0; guard < 4; guard++) {
        int avail = SDL_GetAudioStreamAvailable(a->stream);
        if (avail >= (int)(DUEL_AUDIO_CHUNK * 2 * sizeof(float)) * 2) break;
        memset(a->mixbuf, 0, DUEL_AUDIO_CHUNK * 2 * sizeof(float));
        DuelAudio_MixChunk(&a->st, a->mixbuf, DUEL_AUDIO_CHUNK);
        DuelAudio_NormalizeChunk(a->mixbuf, DUEL_AUDIO_CHUNK);
        SDL_PutAudioStreamData(a->stream, a->mixbuf,
                               DUEL_AUDIO_CHUNK * 2 * sizeof(float));
    }
}

void DuelAudio_Destroy(DuelAudio *a) {
    if (!a) return;
    if (a->stream) SDL_DestroyAudioStream(a->stream);
    if (a->dev) SDL_CloseAudioDevice(a->dev);
    for (int i = 0; i < AUDIO__COUNT + 3; i++)
        if (a->wavmem[i]) SDL_free(a->wavmem[i]);
    free(a->mixbuf);
    free(a);
}
