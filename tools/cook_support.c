/* XWADuelVR cook support: cgltf implementation + Aeron_LogWarn stub.
 * Lets duel_cook build without SDL/Aeron runtime. */
#define CGLTF_IMPLEMENTATION
#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf_write.h"

#include <stdarg.h>
#include <stdio.h>

void Aeron_LogWarn(const char* category, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "duel_cook [%s] WARN: ", category ? category : "?");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void Aeron_LogDebug(const char* category, const char* fmt, ...) {
    (void)category; (void)fmt;
}
