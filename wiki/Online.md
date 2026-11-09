# Online

[Home](Home.md) - Files: `src/rr_nete.c/.h`, `src/rr_netip.cpp`, `src/d_clisrv.c/.h`, `src/menus/play-online-*.c`,
`src/menus/transient/pause-game.c`, `src/mserv.c`

## Ring Racers E only plays with Ring Racers E

**Why:** [Low-End Mode](Low-End-Mode.md) and other changes affect the game simulation. If an official client and an E
server ran the same netgame, their simulations could drift apart (desync). Mixing versions has to be impossible, and the
player has to get a clear message instead of a timeout.

How (`rr_nete.c`, `d_clisrv.c`):

- An E client sends `application = "RingRacersE"` (`RR_NETAPP`) in its join request. An E server rejects any other
  application with a "Ring Racers E only" message.
- An E server still advertises `application = "RingRacers"` in its server info, plus a new flag bit `SV_RRE`. **Why:**
  the official game ignores servers with a different application name, so it would never get far enough to ask to join
  and see the message. With "RingRacers" it asks, and gets the explanation.
- An E client requires the `SV_RRE` bit. Connecting to an official server shows a warning right away instead of
  waiting for the connection timeout.

The original join code is in
[`d_clisrv.c`](https://github.com/KartKrewDev/RingRacers/blob/49beb6b74bce839f414b6ea9ad655f4a0c72a801/src/d_clisrv.c#L969).

## No server list, Direct Join only

The server browser would show the official game's public list, which E cannot join. So:

- Opening the server browser shows a message explaining that E plays by **Direct Join** (IP address).
- The "(Public: ...)" label was removed from Host Game.
- The **Make Public** option was removed from Options > Server, and the `advertise` console variable always
  goes back to `No` (with a message). E games are never listed on the official master server.
- Builds without curl have no master server at all; empty stand-in functions in `mserv.c` keep them linking.

## My IP Addresses

Press **Y** in the Online menu, in Host Game or in the host's pause menu (a "<Y> My IP Addresses" hint is shown in the
bottom left corner), or use the console command `rrmyip`. A message box shows the
addresses other players can use to join, with the server port. It closes with A, B, X or Y.

Y and not X: in Ring Racers menus, X already means "back". The "List via <master server>" strip at the bottom of the
online menus is not drawn, because Ring Racers E has no server list; in the Online and Host Game menus the hint takes its place.

**Why:** players hosting for friends usually do not know their LAN or VPN address, and the official game has no way to
show it.

`rr_netip.cpp` lists the IPv4 addresses of network adapters that are up:

- LAN and Wi-Fi addresses.
- LAN-style VPNs, named when recognized: ZeroTier, Hamachi, Radmin, Tailscale.
- Loopback and `169.254.x.x` (no network) are skipped.
- A **public** address on a regular adapter (for example a modem in bridge mode) is **never shown**, only counted as
  hidden. **Why:** players share screenshots of this window; it must not leak their real public IP.

On Windows, `GetAdaptersAddresses` is loaded with `LoadLibrary`, so the game does not need to link `iphlpapi`.
`rr_netip.cpp` only includes system headers, to avoid clashes between `windows.h` and the game's macros.

## Low-End Mode in netgames

Low-End Mode changes the simulation, so every machine must use the same value. Rules (`rr_nete.c`):

- The `rrnetlowend` netvar is the game's Low-End setting. Every machine reads it in `P_LoadLevel`.
- The server wants Low-End if the host has it on **or** any connected player uses it.
- The server only changes `rrnetlowend` **outside** a course (intermission, voting), so a whole course always uses one
  value. Before anybody has joined, it is set directly.
- A player with Low-End on who joins a game without it **spectates** until the course ends; then the whole game switches
  to Low-End. The waiting players are a bit mask in the `rrnetlowwait` netvar, honored by `K_CheckSpectateStatus`
  identically on every machine.
- If that player leaves, the host's own setting comes back when the course ends.
- A player without Low-End who joins a Low-End game plays in Low-End Mode, with a notice.
- The join request carries a new `rrlowend` field (only read when the application is `RingRacersE`).

**Why it is safe for players joining mid-course:** the netvar change is applied on the same tic on every machine
(netcommands arrive in order), and a joining player loads the current netvars before `P_LoadLevel` runs.
