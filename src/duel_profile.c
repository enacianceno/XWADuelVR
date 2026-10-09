/* XWADuelVR M7 profile bodies. See duel_profile.h. */
#include "duel_profile.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void DuelPrefs_Defaults(DuelPrefs *p) {
    memset(p, 0, sizeof(*p));
    p->vol_master = 0.8f;
    p->vol_music = 0.65f;
    p->vol_sfx = 1.0f;
    p->sens = 1.0f;
    p->dead = 0.15f;
}

static float duel_pclamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void duel_apply(DuelPrefs *p, const char *key, const char *val) {
    if (!strcmp(key, "vol_master")) p->vol_master = duel_pclamp((float)atof(val), 0.0f, 1.0f);
    else if (!strcmp(key, "vol_music")) p->vol_music = duel_pclamp((float)atof(val), 0.0f, 1.0f);
    else if (!strcmp(key, "vol_sfx")) p->vol_sfx = duel_pclamp((float)atof(val), 0.0f, 1.0f);
    else if (!strcmp(key, "sens")) p->sens = duel_pclamp((float)atof(val), 0.5f, 1.5f);
    else if (!strcmp(key, "dead")) p->dead = duel_pclamp((float)atof(val), 0.05f, 0.30f);
    else if (!strcmp(key, "last_pilot")) {
        size_t n = 0;
        while (n < DUEL_PILOT_NAME_MAX && val[n] &&
                ((val[n] >= 'A' && val[n] <= 'Z') ||
                 (val[n] >= '0' && val[n] <= '9') ||
                 val[n] == '_' || val[n] == '-')) {
            p->last_pilot[n] = val[n];
            n++;
        }
        p->last_pilot[n] = '\0';
    }
}

int DuelPrefs_Load(DuelPrefs *p, const char *dir) {
    char path[600];
    FILE *f;
    DuelPrefs_Defaults(p);
    if (!dir) return 0;
    snprintf(path, sizeof(path), "%s/%s", dir, DUEL_PREFS_FILE);
    f = fopen(path, "r");
    if (!f) return 0;
    {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            char *eq = strchr(line, '=');
            char *nl;
            if (!eq || eq == line) continue;
            *eq = '\0';
            nl = strchr(eq + 1, '\n');
            if (nl) *nl = '\0';
            nl = strchr(eq + 1, '\r');
            if (nl) *nl = '\0';
            duel_apply(p, line, eq + 1);
        }
    }
    fclose(f);
    return 1;
}

int DuelPrefs_Save(const DuelPrefs *p, const char *dir) {
    char path[600];
    FILE *f;
    if (!dir || !p) return 0;
    snprintf(path, sizeof(path), "%s/%s", dir, DUEL_PREFS_FILE);
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "vol_master=%.3f\nvol_music=%.3f\nvol_sfx=%.3f\nsens=%.3f\ndead=%.3f\nlast_pilot=%s\n",
            p->vol_master, p->vol_music, p->vol_sfx, p->sens, p->dead, p->last_pilot);
    fclose(f);
    return 1;
}

/* Pilot names are sanitized to a safe file-stem charset (also the
 * stroke-font charset, so names always render). */
static void duel_pilot_sanitize(const char *src, char *dst) {
    static const char *ok =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    size_t n = 0;
    if (!src) src = "";
    while (*src && n < DUEL_PILOT_NAME_MAX) {
        char c = *src++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (strchr(ok, c)) dst[n++] = c;
    }
    dst[n] = '\0';
}

static int duel_pilot_path(char *out, size_t cap, const char *dir,
                           const char *name) {
    char clean[DUEL_PILOT_NAME_MAX + 1];
    if (!out || !cap || !dir || !name) return 0;
    duel_pilot_sanitize(name, clean);
    if (!clean[0]) return 0;
    snprintf(out, cap, "%s/pilot_%s.cfg", dir, clean);
    return 1;
}

void DuelPilot_Defaults(DuelPilot *p, const char *name) {
    memset(p, 0, sizeof(*p));
    duel_pilot_sanitize(name, p->name);
    p->last_diff = 1; /* PILOT baseline */
    p->last_win = -1;
}

int DuelPilot_Load(DuelPilot *p, const char *dir, const char *name) {
    char path[600];
    FILE *f;
    DuelPilot_Defaults(p, name);
    if (!duel_pilot_path(path, sizeof(path), dir, name)) return 0;
    f = fopen(path, "r");
    if (!f) return 0;
    {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            char *eq = strchr(line, '=');
            char *nl;
            if (!eq || eq == line) continue;
            *eq = '\0';
            nl = strchr(eq + 1, '\n');
            if (nl) *nl = '\0';
            if (!strcmp(line, "combats")) p->combats = atoi(eq + 1);
            else if (!strcmp(line, "wins")) p->wins = atoi(eq + 1);
            else if (!strcmp(line, "losses")) p->losses = atoi(eq + 1);
            else if (!strcmp(line, "kills")) p->kills = atoi(eq + 1);
            else if (!strcmp(line, "last_diff"))
                p->last_diff = (int)duel_pclamp((float)atoi(eq + 1), 0.0f, 2.0f);
            else if (!strcmp(line, "last_win"))
                p->last_win = atoi(eq + 1) ? 1 : 0;
        }
    }
    fclose(f);
    if (p->combats < 0) p->combats = 0;
    if (p->wins < 0) p->wins = 0;
    if (p->losses < 0) p->losses = 0;
    if (p->kills < 0) p->kills = 0;
    return 1;
}

int DuelPilot_Save(const DuelPilot *p, const char *dir) {
    char path[600];
    FILE *f;
    if (!p || !duel_pilot_path(path, sizeof(path), dir, p->name)) return 0;
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "name=%s\ncombats=%d\nwins=%d\nlosses=%d\nkills=%d\nlast_diff=%d\nlast_win=%d\n",
            p->name, p->combats, p->wins, p->losses, p->kills,
            p->last_diff, p->last_win);
    fclose(f);
    return 1;
}

int DuelPilot_Delete(const char *dir, const char *name) {
    char path[600];
    if (!duel_pilot_path(path, sizeof(path), dir, name)) return 0;
    return remove(path) == 0 ? 1 : 0;
}

void DuelPilot_Record(DuelPilot *p, int win, int kills, int diff) {
    if (!p) return;
    p->combats++;
    if (win) p->wins++;
    else p->losses++;
    if (kills > 0) p->kills += kills;
    p->last_diff = diff < 0 ? 0 : (diff > 2 ? 2 : diff);
    p->last_win = win ? 1 : 0;
}

int DuelPilot_Scan(const char *dir, char names[][DUEL_PILOT_NAME_MAX + 1],
                   int cap) {
    DIR *d;
    struct dirent *e;
    int n = 0, i;
    if (!dir || !names || cap <= 0) return 0;
    d = opendir(dir);
    if (!d) return 0;
    while ((e = readdir(d)) != NULL && n < cap) {
        size_t len = strlen(e->d_name);
        /* pilot_<NAME>.cfg */
        if (len < 11) continue;
        if (strncmp(e->d_name, "pilot_", 6)) continue;
        if (strcmp(e->d_name + len - 4, ".cfg")) continue;
        {
            char raw[DUEL_PILOT_NAME_MAX + 5];
            size_t nl = len - 10;
            if (nl >= sizeof(raw)) nl = sizeof(raw) - 1;
            memcpy(raw, e->d_name + 6, nl);
            raw[nl] = '\0';
            duel_pilot_sanitize(raw, names[n]);
            if (!names[n][0]) continue;
            /* De-dup (case variants collapse to one stem). */
            for (i = 0; i < n; i++) {
                if (!strcmp(names[i], names[n])) break;
            }
            if (i == n) n++;
        }
    }
    closedir(d);
    /* Deterministic order for the menu list. */
    for (i = 1; i < n; i++) {
        char tmp[DUEL_PILOT_NAME_MAX + 1];
        int j = i;
        memcpy(tmp, names[i], sizeof(tmp));
        while (j > 0 && strcmp(names[j - 1], tmp) > 0) {
            memcpy(names[j], names[j - 1], sizeof(names[j]));
            j--;
        }
        memcpy(names[j], tmp, sizeof(tmp));
    }
    return n;
}
