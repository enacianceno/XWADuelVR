/* Standalone SDL3 audio probe (runs headless, no XR): opens the default
 * playback device, loads a WAV, streams it in a loop for ~6 s, and prints
 * device/stream state. Proves the Quest audio path the game uses.
 * Usage: sdl_audio_probe file.wav
 */
#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    SDL_AudioSpec file_spec, want, have;
    Uint8 *wav = NULL;
    Uint32 wavlen = 0;
    SDL_AudioDeviceID dev;
    SDL_AudioStream *stream;

    if (argc != 2) {
        printf("usage: sdl_audio_probe file.wav\n");
        return 2;
    }
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        printf("PROBE init_fail %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_LoadWAV(argv[1], &file_spec, &wav, &wavlen) || !wav) {
        printf("PROBE load_fail %s\n", SDL_GetError());
        return 1;
    }
    printf("PROBE wav rate=%d ch=%d fmt=0x%x bytes=%u\n", file_spec.freq,
           file_spec.channels, (unsigned)file_spec.format, wavlen);
    SDL_zero(want);
    want.format = SDL_AUDIO_F32;
    want.channels = 2;
    want.freq = 44100;
    dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
    if (!dev) {
        printf("PROBE nodevice %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_GetAudioDeviceFormat(dev, &have, NULL)) {
        printf("PROBE noformat %s\n", SDL_GetError());
        return 1;
    }
    printf("PROBE device rate=%d ch=%d fmt=0x%x\n", have.freq, have.channels,
           (unsigned)have.format);
    stream = SDL_CreateAudioStream(&file_spec, &have);
    if (!stream || !SDL_BindAudioStream(dev, stream)) {
        printf("PROBE stream_fail %s\n", SDL_GetError());
        return 1;
    }
    SDL_ResumeAudioDevice(dev);
    /* Feed ~6 s of audio, then report drained amount. */
    {
        Uint64 t0 = SDL_GetTicksNS();
        long fed = 0;
        while ((SDL_GetTicksNS() - t0) < 6000000000ull) {
            int avail = SDL_GetAudioStreamAvailable(stream);
            if (avail < 65536) {
                SDL_PutAudioStreamData(stream, wav, (int)wavlen);
                fed += wavlen;
            }
            SDL_Delay(50);
        }
        printf("PROBE fed=%ld avail_end=%d\n", fed,
               SDL_GetAudioStreamAvailable(stream));
    }
    printf("PROBE DONE device_playing\n");
    SDL_DestroyAudioStream(stream);
    SDL_CloseAudioDevice(dev);
    SDL_free(wav);
    SDL_Quit();
    return 0;
}
