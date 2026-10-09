/* XWADuelVR audio core: event detection + mixer. SDL-free (unit-tested). */
#include "duel_audio.h"

#include <string.h>

/* Base gains (XWA .lst volumes / 100 where known). */
#define G_FIRE 0.70f
#define G_HIT 0.80f
#define G_BOOM 0.85f
#define G_ENGINE 0.55f
#define G_TIE_ENG 0.50f
#define G_MUSIC 0.65f /* P1.5a: 0.40 was nearly inaudible under engines (ear) */
#define DUCK_DEPTH 0.65f /* music gain left at full throttle (was 0.55) */
#define G_WARN 0.60f
#define G_R2 0.50f

/* Audibility ranges (m), from .lst distances. */
#define R_FIRE 2000.0f
#define R_HIT 2500.0f
#define R_BOOM 6000.0f
#define R_TIE_ENG 1500.0f

void DuelAudio_InitState(DuelAudioState *st) {
    memset(st, 0, sizeof(*st));
    st->last_alive_p = 1;
    { /* M5 */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) st->last_alive_t[k] = 1;
        for (k = 0; k < DUEL_ALLY_COUNT; k++) st->last_alive_a[k] = 1;
    }
}

static void trigger(DuelAudioState *st, DuelAudioEvent ev, float lg, float rg) {
    const DuelSound *snd = &st->sounds[ev];
    int slot = -1;
    if (!st->ready || !snd->pcm || snd->frames == 0) return;
    for (int i = 0; i < DUEL_AUDIO_VOICES; i++) {
        if (!st->voices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0; /* steal oldest slot */
    {
        DuelVoice *v = &st->voices[slot];
        v->snd = snd;
        v->pos = 0.0;
        v->step = (double)snd->rate / (double)DUEL_AUDIO_RATE;
        v->lgain = lg;
        v->rgain = rg;
        v->active = 1;
        v->loop = 0;
    }
}

/* Positional one-shot at world `at`. */
static void trigger_at(DuelAudioState *st, DuelAudioEvent ev, DuelVec3 at,
                       DuelListener lis, float base, float maxd) {
    DuelVec3 d = duel_v3_sub(at, lis.pos);
    float dist = duel_v3_len(d);
    float g = base * duel_audio_atten(dist, maxd);
    float l, r;
    if (g <= 0.001f) return;
    if (dist > 1e-3f) {
        float x = duel_v3_dot(duel_v3_scale(d, 1.0f / dist), lis.right);
        duel_audio_pan(x, &l, &r);
    } else {
        l = r = 0.7071f;
    }
    trigger(st, ev, g * l, g * r);
}

void DuelAudio_PollEvents(DuelAudioState *st, const DuelState *s,
                          DuelListener lis, float dt) {
    /* Fire: muzzle counter advanced (a reset wraps it to 0: resync, silent). */
    if (s->player.muzzle != st->last_muzzle_p) {
        if (s->player.muzzle > st->last_muzzle_p)
            trigger(st, AUDIO_PLAYER_FIRE, G_FIRE * 0.7071f, G_FIRE * 0.7071f);
        st->last_muzzle_p = s->player.muzzle;
    }
    { /* M5: per-ship fire/hit/death (ties + allies) */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (s->ties[k].muzzle != st->last_muzzle_t[k]) {
                if (s->ties[k].muzzle > st->last_muzzle_t[k])
                    trigger_at(st, AUDIO_TIE_FIRE, s->ties[k].pos, lis, G_FIRE, R_FIRE);
                st->last_muzzle_t[k] = s->ties[k].muzzle;
            }
        }
        for (k = 0; k < DUEL_ALLY_COUNT; k++) {
            if (s->allies[k].muzzle != st->last_muzzle_a[k]) {
                if (s->allies[k].muzzle > st->last_muzzle_a[k])
                    trigger_at(st, AUDIO_PLAYER_FIRE, s->allies[k].pos, lis, G_FIRE, R_FIRE);
                st->last_muzzle_a[k] = s->allies[k].muzzle;
            }
        }
    }
    /* Hits: hp dropped while alive. */
    if (s->player.alive && s->player.hp < st->last_hp_p)
        trigger(st, AUDIO_PLAYER_HIT, G_HIT * 0.7071f, G_HIT * 0.7071f);
    { /* M5 */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (s->ties[k].alive && s->ties[k].hp < st->last_hp_t[k])
                trigger_at(st, AUDIO_TIE_HIT, s->ties[k].pos, lis, G_HIT, R_HIT);
        }
        for (k = 0; k < DUEL_ALLY_COUNT; k++) {
            if (s->allies[k].alive && s->allies[k].hp < st->last_hp_a[k])
                trigger_at(st, AUDIO_TIE_HIT, s->allies[k].pos, lis, G_HIT, R_HIT);
        }
    }
    st->last_hp_p = s->player.hp;
    { /* M5 */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) st->last_hp_t[k] = s->ties[k].hp;
        for (k = 0; k < DUEL_ALLY_COUNT; k++) st->last_hp_a[k] = s->allies[k].hp;
    }
    /* Deaths: alive flip. */
    if (st->last_alive_p && !s->player.alive)
        trigger(st, AUDIO_PLAYER_EXPLODE, G_BOOM * 0.7071f, G_BOOM * 0.7071f);
    { /* M5: R2 kill-jingle on tie deaths (shared victory); ally deaths
         * stay grim and silent. */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (st->last_alive_t[k] && !s->ties[k].alive) {
                trigger_at(st, AUDIO_TIE_EXPLODE, s->ties[k].pos, lis, G_BOOM, R_BOOM);
                trigger(st, AUDIO_R2, G_R2 * 0.7071f, G_R2 * 0.7071f);
            }
        }
        for (k = 0; k < DUEL_ALLY_COUNT; k++) {
            if (st->last_alive_a[k] && !s->allies[k].alive)
                trigger_at(st, AUDIO_TIE_EXPLODE, s->allies[k].pos, lis, G_BOOM, R_BOOM);
        }
    }
    st->last_alive_p = s->player.alive;
    { /* M5 */
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) st->last_alive_t[k] = s->ties[k].alive;
        for (k = 0; k < DUEL_ALLY_COUNT; k++) st->last_alive_a[k] = s->allies[k].alive;
    }
    /* Low-HP warning with cooldown. */
    if (st->warn_cd > 0.0f) st->warn_cd -= dt;
    if (s->player.alive && s->player.hp < 30.0f && st->warn_cd <= 0.0f) {
        trigger(st, AUDIO_WARNING, G_WARN * 0.7071f, G_WARN * 0.7071f);
        st->warn_cd = 6.0f;
    }
    /* Engine loops follow life + throttle. */
    if (!st->loop_engine_p.active && s->player.alive && st->engine_player.pcm) {
        st->loop_engine_p.snd = &st->engine_player;
        st->loop_engine_p.pos = 0.0;
        st->loop_engine_p.active = 1;
        st->loop_engine_p.loop = 1;
    }
    if (!s->player.alive) st->loop_engine_p.active = 0;
    if (st->loop_engine_p.active) {
        float g = G_ENGINE * (0.35f + 0.65f * s->player.throttle);
        st->loop_engine_p.step = (double)st->engine_player.rate /
                                 (double)DUEL_AUDIO_RATE *
                                 (0.92 + 0.16 * s->player.throttle);
        st->loop_engine_p.lgain = st->loop_engine_p.rgain = g * 0.7071f;
    }
    /* M5: the single TIE scream follows the nearest LIVING tie. */
    if (!st->loop_engine_t.active && Duel_TiesAlive(s) > 0 && st->engine_tie.pcm) {
        st->loop_engine_t.snd = &st->engine_tie;
        st->loop_engine_t.pos = 0.0;
        st->loop_engine_t.active = 1;
        st->loop_engine_t.loop = 1;
    }
    if (Duel_TiesAlive(s) == 0) st->loop_engine_t.active = 0;
    if (st->loop_engine_t.active) {
        /* nearest living tie to the listener (NOT the player's auto-
         * target: the scream must survive the player's own death). */
        DuelVec3 tp = s->ties[0].pos;
        float bd = 1e30f;
        int k;
        for (k = 0; k < DUEL_TIE_COUNT; k++) {
            if (!s->ties[k].alive) continue;
            {
                float dd = duel_v3_len(duel_v3_sub(s->ties[k].pos, lis.pos));
                if (dd < bd) { bd = dd; tp = s->ties[k].pos; }
            }
        }
        DuelVec3 d = duel_v3_sub(tp, lis.pos);
        float dist = duel_v3_len(d);
        float g = G_TIE_ENG * duel_audio_atten(dist, R_TIE_ENG);
        float l = 0.7071f, r = 0.7071f;
        if (dist > 1e-3f) {
            float x = duel_v3_dot(duel_v3_scale(d, 1.0f / dist), lis.right);
            duel_audio_pan(x, &l, &r);
        }
        st->loop_engine_t.step =
            (double)st->engine_tie.rate / (double)DUEL_AUDIO_RATE;
        st->loop_engine_t.lgain = g * l;
        st->loop_engine_t.rgain = g * r;
    }
    if (!st->loop_music.active && st->music.pcm) {
        st->loop_music.snd = &st->music;
        st->loop_music.pos = 0.0;
        st->loop_music.step =
            (double)st->music.rate / (double)DUEL_AUDIO_RATE;
        st->loop_music.active = 1;
        st->loop_music.loop = 1;
    }
    /* Ducking: throttle up => engines roar, music steps back. */
    if (st->loop_music.active) {
        float g = G_MUSIC * (1.0f - (1.0f - DUCK_DEPTH) *
                                       (s->player.alive ? s->player.throttle : 0.0f));
        st->loop_music.lgain = st->loop_music.rgain = g;
    }
}

static void mix_voice(DuelVoice *v, float *out, unsigned frames) {
    const DuelSound *s = v->snd;
    unsigned n = s->frames;
    int ch = s->channels;
    for (unsigned f = 0; f < frames; f++) {
        unsigned i0 = (unsigned)v->pos;
        float frac, a0, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
        if (i0 >= n) {
            if (!v->loop || n == 0) {
                v->active = 0;
                return;
            }
            v->pos -= n;
            i0 = (unsigned)v->pos;
        }
        frac = (float)(v->pos - i0);
        a0 = (float)s->pcm[i0 * ch];
        if (ch > 1) b0 = (float)s->pcm[i0 * ch + 1];
        if (i0 + 1 < n) {
            a1 = (float)s->pcm[(i0 + 1) * ch];
            if (ch > 1) b1 = (float)s->pcm[(i0 + 1) * ch + 1];
        } else if (v->loop && n > 0) {
            a1 = (float)s->pcm[0];
            if (ch > 1) b1 = (float)s->pcm[1];
        } else {
            a1 = a0;
            b1 = b0;
        }
        if (ch > 1) {
            /* Stereo: direct with per-side gain. */
            out[f * 2] += (a0 + (a1 - a0) * frac) * v->lgain;
            out[f * 2 + 1] += (b0 + (b1 - b0) * frac) * v->rgain;
        } else {
            float m = (a0 + (a1 - a0) * frac);
            out[f * 2] += m * v->lgain;
            out[f * 2 + 1] += m * v->rgain;
        }
        v->pos += v->step;
    }
}

void DuelAudio_MixChunk(DuelAudioState *st, float *out, unsigned frames) {
    for (int i = 0; i < DUEL_AUDIO_VOICES; i++)
        if (st->voices[i].active) mix_voice(&st->voices[i], out, frames);
    if (st->loop_engine_p.active)
        mix_voice(&st->loop_engine_p, out, frames);
    if (st->loop_engine_t.active)
        mix_voice(&st->loop_engine_t, out, frames);
    if (st->loop_music.active) mix_voice(&st->loop_music, out, frames);
}

void DuelAudio_NormalizeChunk(float *out, unsigned frames) {
    float k = DUEL_AUDIO_MASTER / 32768.0f;
    for (unsigned i = 0; i < frames * 2u; i++) {
        float v = out[i] * k;
        if (v > 1.0f) v = 1.0f;
        else if (v < -1.0f) v = -1.0f;
        out[i] = v;
    }
}

void DuelAudio_LoopBlend(int16_t *pcm, unsigned frames, int ch, unsigned x) {
    unsigned i, c;
    if (!pcm || ch < 1 || x == 0 || frames <= x) return;
    for (i = 0; i < x; i++) {
        float t = (float)i / (float)x;
        for (c = 0; c < (unsigned)ch; c++) {
            float tail = (float)pcm[(frames - x + i) * ch + c];
            float head = (float)pcm[i * ch + c];
            pcm[(frames - x + i) * ch + c] =
                duel_audio_clamp(tail * (1.0f - t) + head * t);
        }
    }
}
