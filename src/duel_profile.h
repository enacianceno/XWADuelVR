/* XWADuelVR M7 profile: prefs + pilot stats persistence.
 *
 * Plain key=value text under the app files dir (same root as
 * duel_assets/, no VFS, no engine). Prefs and pilot stats are
 * SEPARATE files by user order. Tolerant reader: missing file or
 * bad lines fall back to defaults, never crash.
 *
 * This is a DuelVR-native profile, NOT an original-XWA pilot file
 * (no engine save format is reused; see M7 checkpoint).
 */
#ifndef XWADUELVR_PROFILE_H
#define XWADUELVR_PROFILE_H

#define DUEL_PREFS_FILE "duel_prefs.cfg"
#define DUEL_PILOT_NAME_MAX 16
#define DUEL_PILOT_MAX 8

typedef struct DuelPrefs {
    float vol_master, vol_music, vol_sfx;
    float sens;
    float dead;
    char last_pilot[DUEL_PILOT_NAME_MAX + 1];
} DuelPrefs;

void DuelPrefs_Defaults(DuelPrefs *p);
/* Load from <dir>/duel_prefs.cfg (1) or defaults when missing (0). */
int DuelPrefs_Load(DuelPrefs *p, const char *dir);
/* Save to <dir>/duel_prefs.cfg (1 ok, 0 io error). */
int DuelPrefs_Save(const DuelPrefs *p, const char *dir);

/* Per-pilot Combat Simulation stats (<dir>/pilot_<NAME>.cfg). */
typedef struct DuelPilot {
    char name[DUEL_PILOT_NAME_MAX + 1];
    int combats;
    int wins;
    int losses;
    int kills;
    int last_diff; /* DuelDifficulty of last recorded combat */
    int last_win;  /* 1 victory, 0 defeat, -1 none yet */
} DuelPilot;

void DuelPilot_Defaults(DuelPilot *p, const char *name);
/* Load <dir>/pilot_<NAME>.cfg (1) or fresh defaults (0). */
int DuelPilot_Load(DuelPilot *p, const char *dir, const char *name);
int DuelPilot_Save(const DuelPilot *p, const char *dir);
int DuelPilot_Delete(const char *dir, const char *name);
/* Record ONE combat result (call exactly once per combat). */
void DuelPilot_Record(DuelPilot *p, int win, int kills, int diff);
/* List saved pilots (names only, up to cap; returns count). */
int DuelPilot_Scan(const char *dir, char names[][DUEL_PILOT_NAME_MAX + 1],
                   int cap);

#endif /* XWADUELVR_PROFILE_H */
