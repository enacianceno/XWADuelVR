/* Stub for offline imc2wav: provides Aeron_LogError without Aeron. */
#ifndef IMCI_AERON_LOG_H
#define IMCI_AERON_LOG_H
#include <stdio.h>
#define Aeron_LogError(tag, msg) \
    do { fprintf(stderr, "ERR %s: %s\n", (tag), (msg)); } while (0)
#endif
