# M11 NEAR HOME PANEL (2026-10-09/10, código + APK, validación física pendiente)

Status: BUILT, host + device-CPU verified, INSTALLED. Milestone NOT
physically validated: user tests in visor.

Session scope: XWADuelVR ONLY (XWAQuest + shared libs read-only).
No reset/clean, no overwritten builds.

User report on M10: touch DID select on contact (mechanism works),
but the panel sat far/off-side and ran FARTHER away after each
selection, putting it out of reach.

## 1. Preservation

- Git `d44c46f` + uncommitted M9+M10+M11 work (left for review).
- Installed on Quest 3487C10H9T0BKZ (Quest 3S):
  `v3.10-M11-nearpanel` (`install -r`, prefs/pilots intact). Prior
  APKs intact. Controls / easy AI / battle sizes unchanged.

## 2. Root cause of the runaway

The panel was re-anchored from the LIVE head pose on every screen
change, 1.1 m out. To touch it the user leans/turns; the tap changes
screen; the new panel anchors 1.1 m ahead of the leaned/turned pose:
each selection pushes it farther / more sideways. Off-side placement
came from the same head-pose anchor (head turned at transition).

## 3. Fix: canonical near home panel

- Placement is now canonical, not head-driven:
  `DuelRender_PlaceMenuHome` anchors the panel 0.5 m in front of the
  CALIBRATED eye point, facing it (cockpit-fixed: the same frame as
  the ship, which the user confirms looks centered). Every screen
  lands in the same spot: near, centered, never moves on selection.
- Math: new pure `duel_home_anchor` (LOCAL = neutral_pos +
  Ry(neutral_yaw) * seat_offset) inverts the eye path's first step;
  neutralizing the anchor returns the offset exactly (CPU roundtrip
  test). Pre-cal fallback keeps the live head pose (~2 s at boot;
  the M9 gen 0->N re-place swaps in the home anchor when cal lands).
- Size: geometry keeps the 1.1 m design's angular size (x0.4545:
  0.773 m wide, rows 4.8 cm, text identical readability) so it reads
  the same up close. Panel draws depth-off (always on top, verified
  from the pipeline flags), so nothing occludes it at 0.5 m.
- Touch near-zone (30 cm) + press (<= 2 cm, release past 5 cm /
  off-panel) unchanged thresholds.

## 4. Touch arbitration fix (two hands)

- M10 returned press from ANY hand but hover from the winner: a gap
  press on one hand + hover on the other could misfire. M11 picks ONE
  winner hand (pressed-on-a-row > any pressed near > any near) and
  reports BOTH hover and press from it (`menu_touch`). Gap presses
  block, never misfire; slider drags keep winning mid-drag.

## 5. Tests

- `tests/test_render_rules.py` (host): 145/145 PASS (143 + 2 M11:
  home placement at 0.5 m, winner arbitration). One M10 rule updated
  to the winner-press return (same touch guarantee, stricter).
- `tests/test_duel.c` on device (`/data/local/tmp/test_duel_m11`):
  ALL DUEL TESTS PASS, incl. new `home_anchor` (roundtrip + size
  pins). Layout tests green unchanged (relative pins).
- Native rebuilt; installed APK verified by dumpsys version.

## 6. Build

- `XWADuelVR-M11-NEARPANEL-debug.apk`, v3.10-M11-nearpanel,
  2237383 bytes,
  SHA-256 `63E7E1008FADF2BD9E5EC736AC43A13000786F17E1A05F21558675D446D3D0CC`.
- Built: Build-Target.ps1 + cached gradle-8.5 offline assembleDebug
  (BUILD SUCCESSFUL). Installed with `-r`.
- Note: USB adb dropped mid-session (cable); install went over the
  WiFi transport to the SAME Quest (serial verified 3487C10H9T0BKZ).
  Nothing was disconnected; both transports left as found.

## 7. Visor test protocol (user)

1. Boot: menu appears ~50 cm ahead, centered like the ship, readable;
   it must NOT jump when you select (all screens, same spot).
2. Touch rows with the fingertip (no pinch): highlight on approach,
   click on contact; sliders by finger drag.
3. Reach check: can you comfortably touch corner rows? Report any
   row that feels out of reach or hard to read.
4. Pause mid-combat: panel centered too; resume/restart flow as M10.

## Next step

ONE: user runs §7 and reports (reach, centering, stability across
selections). No physical validation declared until then.
