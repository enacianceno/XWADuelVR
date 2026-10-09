/* XWADuelVR flight/tuning constants. No menus: tweak here, rebuild. */
#ifndef XWADUELVR_FLIGHT_CONFIG_H
#define XWADUELVR_FLIGHT_CONFIG_H

/* --- Player X-Wing --- */
#define DUEL_PLAYER_MIN_SPEED   40.0f   /* m/s at throttle 0 */
#define DUEL_PLAYER_MAX_SPEED   160.0f  /* m/s at throttle 1 */
#define DUEL_PLAYER_ACCEL       120.0f  /* m/s^2 toward target speed */
#define DUEL_PLAYER_PITCH_RATE  1.6f    /* rad/s at full deflection */
#define DUEL_PLAYER_YAW_RATE    0.9f    /* rad/s */
#define DUEL_PLAYER_ROLL_RATE   2.2f    /* rad/s */
#define DUEL_PLAYER_HP          100.0f
#define DUEL_PLAYER_RADIUS      6.0f    /* bounding sphere, meters */
#define DUEL_PLAYER_LASER_COOLDOWN 0.16f /* s between shots */
#define DUEL_PLAYER_LASER_DAMAGE   12.0f
#define DUEL_PLAYER_LASER_SPEED    600.0f /* m/s muzzle + ship speed */

/* --- TIE Fighter --- */
#define DUEL_TIE_MIN_SPEED      50.0f
#define DUEL_TIE_MAX_SPEED      120.0f  /* M5: was 135 (player 160
                                           walks away; catchable) */
#define DUEL_TIE_ACCEL          100.0f
#define DUEL_TIE_TURN_RATE      0.75f   /* M5: was 0.9 (player yaw 0.9 +
                                           pitch 1.6 dominate; tail glue
                                           broken, overshoots cross) */
#define DUEL_TIE_HP             70.0f   /* M3.8c: was 100 (6 hits) */
#define DUEL_TIE_RADIUS         7.0f    /* M3.8c: was 5 (forgiving) */
#define DUEL_TIE_LASER_COOLDOWN 0.75f   /* M3.8c: was 0.55 (breathing) */

/* M5 allies (X-wing frame, gentler than the player so the player still
 * gets kills; plain chase, no evade states). */
#define DUEL_ALLY_HP            100.0f
#define DUEL_ALLY_RADIUS        6.0f    /* same airframe as the player */
#define DUEL_ALLY_MIN_SPEED     50.0f
#define DUEL_ALLY_MAX_SPEED     150.0f
#define DUEL_ALLY_ACCEL         100.0f
#define DUEL_ALLY_TURN_RATE     1.0f
#define DUEL_ALLY_LASER_COOLDOWN 0.70f
#define DUEL_ALLY_LASER_DAMAGE  8.0f
#define DUEL_TIE_LASER_DAMAGE   10.0f
#define DUEL_TIE_LASER_SPEED    550.0f
#define DUEL_TIE_FIRE_CONE_DOT  0.965f  /* ~15 deg half-angle */
#define DUEL_TIE_FIRE_RANGE     700.0f
#define DUEL_TIE_EVADE_DIST     80.0f   /* M3.8b: was 60 (breaks earlier,
                                           longer crossing passes) */
#define DUEL_TIE_EVADE_TIME     2.0f    /* M3.8b: was 1.6 literal */
#define DUEL_TIE_WEAVE          0.5f    /* M3.8c: was 1.0 (steadier target,
                                           easier lead; XWA low-skill mirror:
                                           worse evasion, worse fire) */
#define DUEL_TIE_CHASE_DIST     350.0f  /* preferred attack distance */

/* --- Lasers --- */
#define DUEL_LASER_LIFETIME     2.5f
#define DUEL_LASER_RADIUS       1.2f
#define DUEL_LASER_POOL         64

/* --- Match flow --- */
#define DUEL_RESET_DELAY        2.5f    /* s of explosion before ResetCombat */

/* --- Spawns --- */
#define DUEL_SPAWN_TIE_FWD      1000.0f
#define DUEL_SPAWN_TIE_RIGHT    250.0f
#define DUEL_SPAWN_TIE_UP       120.0f

/* --- Input (replicates XWAQuest M8 TouchConvert semantics) --- */
#define DUEL_DEADZONE           0.15f
/* M6: DUEL_COORD_YAW removed (bank coordination deleted by user
 * order: yaw and roll are independent axes now). */
#define DUEL_THROTTLE_RATE      0.5f    /* lever units per second */
#define DUEL_TRIGGER_FIRE_ON    0.55f
#define DUEL_TRIGGER_FIRE_OFF   0.45f

#endif /* XWADUELVR_FLIGHT_CONFIG_H */
