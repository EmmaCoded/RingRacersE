# Low-End Mode

[Home](Home.md) - Files: `src/rr_config.h`, `src/p_setup.cpp`, `src/p_tick.c`, `src/p_mobj.c`, `src/p_map.c`, `src/k_bot.cpp`,
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
  (The Legacy GL drawing changes are the exception: they only affect drawing, so they follow the option directly,
  also in replays. See [Legacy GL changes](Legacy-GL-Changes.md).)

All the code is inside `#ifdef RR_LOWEND_CODE` blocks. A few of those blocks are always active but give the exact
same result as the original, only cheaper (for example, `K_GetKartSpeed` and `K_PlayerTripwireSpeedThreshold` are
computed once instead of twice in the speed lines code of `p_mobj.c`).

Some savings found while measuring Low-End Mode turned out to be plain bugs of the original code and are active
for everybody: see [Small fixes](Small-Fixes.md#state-changes-cleared-28-kb-every-time).

## What changes

Each change below says why it does not change the race. Numbers are measurements from slow hardware.

### Thinkers (`p_tick.c`, `p_mobj.c`, `p_map.c`)

| Change | Why it is safe |
|---|---|
| **Sleeping rings:** an idle ring using the global `S_RING` animation is marked `rre_sleep` and `P_RunThinkers` skips it. Its frame is computed when it is drawn. 1 tic in 32 it is still checked (it was 1 in 8: with hundreds of rings on a course the check itself was a visible cost, and the ring already wakes up by itself in every case that matters). | An idle ring only changes frame, and that frame only depends on `leveltime`. It wakes up when its state changes, when it is removed and when a magnet touches it. |
| Rings far from every **camera** animate 1 tic in 4 (radius 2000). | Visual only. Cameras, not players: with 8 racers spread out, almost no ring was far from all players. |
| Fixed objects far from the camera (springs, item boxes, capsule boxes, tripwire) think 1 tic in 4. | They only animate; contact with karts is detected by the kart, not by the object. |
| Idle decorations far from every **camera** (cacti, pumpkins...: fixed state, no movement; radius 2500) think 1 tic in 4. | Nothing to update. It used to be "far from every player", but with the racers spread over the course almost no decoration was far from all of them. |
| **Course decoration** (types a course defines itself with `FREESLOT`: flowers, bushes, trees) standing still on the floor, or without gravity, only animates. Umbrella Rushwinds has about 800 of them. | They have no logic of their own in the engine, and one that moves, falls, scales or has a fuse uses the normal path. **Trade-off:** while idle they skip sector actions and the Lua `MobjThinker` hook, so an add-on that scripts its own decoration this way does not run it in Low-End Mode. |
| **Fog** (`MT_EERIEFOG`, about 1050 objects in Umbrella Rushwinds) only animates; if wind pushes it, it moves without `P_CheckPosition`. | It passes through everything and nothing can touch it, but each one asked for a collision check every tic. |
| Finish line sparkles far from every camera are removed. | They recreate themselves when a camera gets close. |
| Attachments of **hidden** bot karts think 1 tic in 4. | Not visible; visible ones always think. |
| Idle floating objects far from players skip `P_ZMovement` and `P_CheckPosition`. | `P_ZMovement` would do nothing, and their `P_CheckPosition` can only affect players. Types with their own collision logic are excluded. |
| `MT_LOOPENDPOINT` (huge radius) skips its `P_CheckPosition` when no player is near. | It can only touch players, so the result would be identical. |
| Invisible objects with no position-dependent sound are not moved. When they become visible again they are placed with `P_SetOrigin` (no interpolation from the old position). | `P_MoveOrigin` runs a full `CheckPosition`; nobody can see or hear them. |
| Prison capsule and its pieces standing still: position recomputed 1 tic in 8 (84 pieces cost 5.7 ms per tic). Decorative pieces only relink, without `P_CheckPosition`. | They do not move. No random numbers are used, so replays are not affected. |
| Flung rings with no player near do not check collisions with objects. With a player near or a magnet, they go back to normal. | Nobody can pick them up there. |
| The debt spike does not check collisions with objects (it cost about 1 ms each). | It is visual only: nothing collides with it. |
| **Sleep by distance:** a flung ring with no magnet, farther than 2500 from **every kart** (bots included), thinks 1 tic in 4. Its fuse keeps counting, so it disappears at the same time as always. | Nobody is close enough to pick it up, and it thinks every tic again as soon as a kart gets near. **Trade-off:** while asleep it moves in steps, so it can come to rest in a slightly different place than in the normal game. The turn is taken from `leveltime` only, so every machine in a netgame skips the same tics. |
| Kart smoke, dust and sparks (boost smoke and flame, drift and brake dust, spindash dust, ring sparks, tire grease) farther than 3000 from every camera are removed. | Visual only and off screen. |
| **Moving without a collision check** (`RRE_CheapMove`, `p_map.c`): visual objects that follow a kart (charge aura, AMP and BLOCK pieces, experience orbs, a ring already picked up) and effects that pass through everything (smoke, dust, sparks, trails) change position directly. In the same sector only the coordinates change; in another one the object is relinked. Used by `P_MoveOrigin`, `P_TryMove` and `P_SceneryTryMove`. | `P_CheckPosition` was the expensive part of every move, and for an object that collides with nothing its only useful result is the floor and ceiling height. **Trade-off:** those heights now come from the sector alone (floors over floors are ignored), which only matters to objects that land, and these do not. |
| Overlays in the same sector as their owner only copy its coordinates. "Underlays" keep the original path. | An overlay sits exactly on its owner. The order inside the sector list matters when drawing underlays, so those are still relinked. |
| Effects without gravity that pass through everything skip `P_SceneryZMovement` and `P_CheckPosition` in `P_SceneryThinker`: only `z += momz`. Explosions and bubbles are excluded. | The same reasoning as the capsule pieces: there is no floor or ceiling for them to hit. |

### Bots (`k_bot.cpp`, `k_kart.c`)

| Change | Why it is safe |
|---|---|
| Bot AI (including one A* search per bot) runs 1 tic in 2, staggered by player. On the other tic the bot repeats its last input **without** one-shot buttons (item use): only steering, accelerate, brake and drift. | At 35 tics per second, one tic of delay is not noticeable, and repeating one-shot buttons would use items twice. |
| The bot waypoint search (about 18 ms per tic with 8 racers) is repeated 1 tic in 8, staggered (it was 1 in 4). A bot shown on screen searches every tic. | Bots move very little in a few tics. **Trade-off:** the position numbers of far bots can update a few tics late. |
| Human players search their waypoint 1 tic in 2, never while respawning or before they have a first waypoint. | One tic is 1/35 of a second; the next waypoint is found on the following tic. |
| Each bot refreshes its distance to the finish line (two A* paths) 1 tic in 4, staggered by player. Human players refresh every tic. | The race position of a bot lags by at most 3 tics (under 0.1 s). **Trade-off:** a bot can be ranked a moment late around a close overtake. |
| Bots that are not shown on screen have no dust, smoke or speed line particles (about 50 ms of thinker time per tic with 8 racers). Invincibility auras, items and shields are not touched. | Visual only, and those particles are not on screen. |

### Effects (`k_kart.c`, `k_race.c`, `objects/item-debris.c`)

| Change | Why it is safe |
|---|---|
| Fewer sparks and particles (1 in 8, 1 in 4 or 1 in 3 depending on the effect). | Visual only. |
| Finish line rainbow: it created about 40 objects per tic (3.2 ms per tic on slow hardware). It is spawned 1 tic in 2 with a 2-tic lifetime. | Visual only; it looks the same. |
| Item debris: no collision at all and a fixed 14-tic lifetime; half the trails. | Visual only. |
| The draft band (the line of sparks between two karts that are drafting) is not created between two rivals. With the player on screen at either end it is drawn as always. | Decoration only: about 5 new objects per tic for each pair. Drafting itself is unchanged. |

### Precipitation (`p_mobj.c`, `p_tick.c`)

The original game creates rain and snow particles over the **whole** map when a course loads: about 100,000 in Launch
Base or Blue Mountain 2 (28 MB), and every moving sector recomputes the floor of all its particles each tic.

In Low-End Mode `P_SpawnPrecipitation` creates nothing. `RRE_PrecipFollow` runs once per tic after the cameras move:
particles farther than 1.5 times the precipitation draw distance from every view are removed, and new ones are created
in random blockmap cells inside that distance (24 tries per tic, up to 512 particles, with a cap per cell). Drawing is
unchanged, because it already only draws the particles inside the draw distance.

**Why it is safe:** precipitation never touches gameplay. It uses its own random number generator, so the game's
generators are not advanced. **Trade-off:** the rain is thinner than the original in tall areas, and it takes a moment
to fill in after the camera jumps to another place.

### Drawing (Legacy GL)

With OpenGL, Low-End Mode also draws the level geometry from a [static geometry cache](Static-Geometry-Cache.md),
and uses a shorter draw distance, a flat sky, no horizon lines and softer sprite lighting without shaders:
see [Legacy GL changes](Legacy-GL-Changes.md#low-end-mode-only-different-picture).

## Tuning and turning parts off

Some parts can be turned off for testing with the optional test file (see [Developer options](Developer-Options.md)):
`ring_sleep=0`, `far_sleep=0`, `cheap_move=0`, `fling_nocollide=0`, `bot_ai_mask=0`, `bot_wp_mask=0`,
`human_wp_mask=0`, `bot_effects`, `nostatic`, and the precipitation limits `precip_max`, `precip_pertic` and
`precip_density`.

## Netgames between different builds

Every change in the tables above that touches the simulation must be the same on all machines. Builds from before
these additions (sleep by distance, moving without a collision check, the new waypoint timings) do not match newer
ones **in Low-End Mode**: all players of a Low-End netgame need the same build. With Low-End Mode off nothing changed.

## Original code

For comparison, the official versions of the main functions involved:
[`P_RunThinkers`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/p_tick.c#L564),
[`P_MobjThinker`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/p_mobj.c#L10821),
[`P_RingThinker`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/p_mobj.c#L4333),
[`K_BuildBotTiccmd`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/k_bot.cpp#L2060).
