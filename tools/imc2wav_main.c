/* Offline IMC (MCMP/VIMA) -> WAV converter.
 * Uses the REAL OpenXWA decoder (xwa/audio/imuse/imcodec.c, read-only)
 * with a ~40-line container walk of our own. Built for ARM, runs on Quest.
 * Usage: imc2wav in.IMC out.wav
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xwa/audio/imuse/imuse.h"

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static void le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

int main(int argc, char **argv) {
    FILE *f;
    uint8_t *file, *out;
    long flen;
    uint16_t nblock, textsz;
    const uint8_t *table, *text;
    size_t dataoff, total = 0;
    ImVimaState vst;
    int i, format = 0, rate = 0, bits = 0, ch = 0, datasz = 0;
    char *dataptr;
    size_t pcmoff, pcmbytes;
    uint8_t wh[44];

    if (argc != 3) {
        fprintf(stderr, "usage: imc2wav in.IMC out.wav\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "open %s failed\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = (uint8_t *)malloc((size_t)flen);
    if (!file || fread(file, 1, (size_t)flen, f) != (size_t)flen) {
        fprintf(stderr, "read failed\n");
        return 1;
    }
    fclose(f);
    if (flen < 8 || memcmp(file, "MCMP", 4) != 0) {
        fprintf(stderr, "not MCMP\n");
        return 1;
    }
    nblock = be16(file + 4);
    table = file + 6;
    textsz = be16(file + 9 * nblock + 6);
    text = file + 9 * nblock + 8;
    dataoff = (size_t)9 * nblock + textsz + 8;
    for (i = 0; i < nblock; i++)
        total += be32(table + 9 * i + 1);
    out = (uint8_t *)malloc(total + 16);
    if (!out) {
        fprintf(stderr, "oom\n");
        return 1;
    }
    ImVimaResetState(&vst);
    {
        size_t off = dataoff, doff = 0;
        for (i = 0; i < nblock; i++) {
            const uint8_t *e = table + 9 * i;
            unsigned idx = e[0];
            uint32_t dec = be32(e + 1), comp = be32(e + 5);
            const uint8_t *tc = text;
            while (idx--) {
                while (*tc) tc++;
                tc++;
            }
            if (memcmp(tc, "VIMA", 4) == 0) {
                ImVimaDecodeBlock(&vst, (int16_t *)(out + doff),
                                  (char *)(file + off), dec);
            } else {
                uint32_t n = dec < comp ? dec : comp;
                memcpy(out + doff, file + off, n);
            }
            off += comp;
            doff += dec;
        }
    }
    dataptr = ImParseSoundHeader((char *)out, &format, &rate, &bits, &ch,
                                 NULL, &datasz, NULL, NULL);
    if (!dataptr || rate <= 0 || (bits != 8 && bits != 16) || ch < 1 || ch > 2) {
        fprintf(stderr, "header parse failed fmt=%d rate=%d bits=%d ch=%d\n",
                format, rate, bits, ch);
        return 1;
    }
    pcmoff = (size_t)(dataptr - (char *)out);
    pcmbytes = total - pcmoff;
    printf("fmt=%d rate=%d bits=%d ch=%d pcmsize=%u\n", format, rate, bits,
           ch, (unsigned)pcmbytes);
    /* WAV/PCM16 writer (mono/stereo preserved). */
    {
        size_t frames = pcmbytes / (2u * (size_t)ch);
        pcmbytes = frames * 2u * (size_t)ch;
        memset(wh, 0, sizeof(wh));
        memcpy(wh, "RIFF", 4);
        le32(wh + 4, (uint32_t)(36 + pcmbytes));
        memcpy(wh + 8, "WAVEfmt ", 8);
        le32(wh + 16, 16);
        le16(wh + 20, 1);
        le16(wh + 22, (uint16_t)ch);
        le32(wh + 24, (uint32_t)rate);
        le32(wh + 28, (uint32_t)(rate * ch * 2));
        le16(wh + 32, (uint16_t)(ch * 2));
        le16(wh + 34, 16);
        memcpy(wh + 36, "data", 4);
        le32(wh + 40, (uint32_t)pcmbytes);
        f = fopen(argv[2], "wb");
        if (!f) {
            fprintf(stderr, "open %s failed\n", argv[2]);
            return 1;
        }
        fwrite(wh, 1, 44, f);
        fwrite(out + pcmoff, 1, pcmbytes, f);
        fclose(f);
        printf("wrote %s (%.1fs)\n", argv[2],
               (double)frames / (double)rate);
    }
    return 0;
}
