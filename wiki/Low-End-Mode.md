# Low-End Mode

[Home](Home.md) - Files: `src/rr_config.h`, `src/p_setup.cpp`, `src/p_tick.c`, `src/p_mobj.c`, `src/k_bot.cpp`,
`src/k_kart.c`, `src/k_race.c`, `src/p_inter.c`, `src/objects/item-debris.c`, `src/hardware/hw_main.cpp`

## What it is

An option in Video > Advanced > **Low-End Mode** (console variable `rrlowend`, off by default). It makes the game
logic and some effects lighter for slow computers.

**Why:** on weak CPUs the game logic, not drawing, is often the bottleneck: a full race with bots runs many object
thinkers, collision checks and pathfinding searches every tic. Most of that work is invisible or repeated, and can
be done less often without changing how a race plays.

## When it is active

`P_LoadLevel` (`p_setup.cpp`) copies the option into `rr_lowend` when a course loads, so a whole course always
uses one value. **Why:** some changes affect the simulation (when things think, what collides), so switching in the
middle of a course would be unpredictable.

- It is never used while watching a replay.
- In netgames everyone uses the server's value: see [Online](Online.md#low-end-mode-in-netgames).
- With the option off, every Low-End branch is skipped (`if (RR_LOWEND ...)`) and the original code runs.
  (The Legacy GL renderer has some changes that are always active: see [Legacy GL changes](Legacy-GL-Changes.md).)

All the code is inside `#ifdef RR_LOWEND_CODE` blocks. A few of those blocks are always active but give the exact
same result as the original, only cheaper (for example, `K_GetKartSpeed` and `K_PlayerTripwireSpeedThreshold` are
computed once instead of twice in the speed lines code of `p_mobj.c`).

## What changes

Each change below says why it does not change the race. Numbers are measurements from slow hardware.

### Thinkers (`p_tick.c`, `p_mobj.c`)

| Change | Why it is safe |
|---|---|
| **Sleeping rings:** an idle ring using the global `S_RING` animation is marked `rre_sleep` and `P_RunThinkers` skips it. Its frame is computed when it is drawn. 1 tic in 8 it is still checked. | An idle ring only changes frame, and that frame only depends on `leveltime`. It wakes up when its state changes, when it is removed and when a magnet touches it. |
| Rings far from every **camera** animate 1 tic in 4 (radius 2000). | Visual only. Cameras, not players: with 8 racers spread out, almost no ring was far from all players. |
| Fixed objects far from the camera (springs, item boxes, capsule boxes, tripwire) think 1 tic in 4. | They only animate; contact with karts is detected by the kart, not by the object. |
| Idle decorations far from every player (cacti, pumpkins...: fixed state, no movement) think 1 tic in 4. | Nothing to update. |
| Finish line sparkles far from every camera are removed. | They recreate themselves when a camera gets close. |
| Attachments of **hidden** bot karts think 1 tic in 4. | Not visible; visible ones always think. |
| Idle floating objects far from players skip `P_ZMovement` and `P_CheckPosition`. | `P_ZMovement` would do nothing, and their `P_CheckPosition` can only affect players. Types with their own collision logic are excluded. |
| `MT_LOOPENDPOINT` (huge radius) skips its `P_CheckPosition` when no player is near. | It can only touch players, so the result would be identical. |
| Invisible objects with no position-dependent sound are not moved. When they become visible again they are placed with `P_SetOrigin` (no interpolation from the old position). | `P_MoveOrigin` runs a full `CheckPosition`; nobody can see or hear them. |
| Prison capsule and its pieces standing still: position recomputed 1 tic in 8 (84 pieces cost 5.7 ms per tic). Decorative pieces only relink, without `P_CheckPosition`. | They do not move. No random numbers are used, so replays are not affected. |
| Flung rings with no player near do not check collisions with objects. With a player near or a magnet, they go back to normal. | Nobody can pick them up there. |
| The debt spike does not check collisions with objects (it cost about 1 ms each). | It is visual only: nothing collides with it. |

### Bots (`k_bot.cpp`, `k_kart.c`)

| Change | Why it is safe |
|---|---|
| Bot AI (including one A* search per bot) runs 1 tic in 2, staggered by player. On the other tic the bot repeats its last input **without** one-shot buttons (item use): only steering, accelerate, brake and drift. | At 35 tics per second, one tic of delay is not noticeable, and repeating one-shot buttons would use items twice. |
| The bot waypoint search (about 18 ms per tic with 8 racers) is repeated 1 tic in 4, staggered. Players shown on screen always search every tic. | Bots move very little in a few tics. |
| Bots that are not shown on screen have no dust, smoke or speed line particles (about 50 ms of thinker time per tic with 8 racers). Invincibility auras, items and shields are not touched. | Visual only, and those particles are not on screen. |

### Effects (`k_kart.c`, `k_race.c`, `objects/item-debris.c`)

| Change | Why it is safe |
|---|---|
| Fewer sparks and particles (1 in 8, 1 in 4 or 1 in 3 depending on the effect). | Visual only. |
| Finish line rainbow: it created about 40 objects per tic (3.2 ms per tic on slow hardware). It is spawned 1 tic in 2 with a 2-tic lifetime. | Visual only; it looks the same. |
| Item debris: no collision at all and a fixed 14-tic lifetime; half the trails. | Visual only. |

### Drawing (Legacy GL)

With OpenGL, Low-End Mode also draws the level geometry from a [static geometry cache](Static-Geometry-Cache.md).

## Tuning and turning parts off

Some parts can be turned off for testing with the optional test file (see [Developer options](Developer-Options.md)):
`ring_sleep=0`, `fling_nocollide=0`, `bot_ai_mask=0`, `bot_wp_mask=0`, `bot_effects`, `nostatic`.

## Original code

For comparison, the official versions of the main functions involved:
[`P_RunThinkers`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/p_tick.c#L564),
[`P_MobjThinker`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/p_mobj.c#L10821),
[`P_RingThinker`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/p_mobj.c#L4333),
[`K_BuildBotTiccmd`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/k_bot.cpp#L2060).
