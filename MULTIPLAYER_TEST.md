# CannonBall DX - Two Player Multiplayer Test

This branch is intentionally back to a small, testable multiplayer core.
The previous shared-traffic experiment is preserved on the
`multiplayer-test-v4-backup` branch, but it is not active here while race
initialization is being stabilized.

## Current test scope

Implemented:

- two CannonBall instances over UDP;
- transport `master` / `slave` roles;
- the first player to press START becomes Player 1 / race leader;
- Player 2 can join during the timeout window;
- Player 1 owns game mode, World/JP course mapping, Time Trial level, music and difficulty;
- Player 2 owns an independent Ferrari colour;
- both instances wait at `GS_INIT_GAME` and launch from a shared start token;
- course mapping, Stage 1 identity, road position and RNG are reset immediately before the synchronized launch;
- the remote Ferrari is drawn as a real perspective-correct OutRun sprite only from `GS_INGAME`, avoiding the start-animation double-car problem.

Not active yet:

- shared traffic authority;
- player-to-player collision;
- authoritative fork choice;
- timer/score/game-over synchronization;
- drift correction / interpolation;
- internet lobby or NAT traversal.

## Configuration

Copy `multiplayer.cfg.example` to `multiplayer.cfg` in each game folder.

### Transport master

```ini
enabled = 1
role = master
host = 127.0.0.1
port = 51337
timeout = 15
start_delay_ms = 1000
```

### Transport slave

```ini
enabled = 1
role = slave
host = 127.0.0.1
port = 51337
timeout = 15
start_delay_ms = 1000
```

For LAN play, replace `127.0.0.1` on the slave with the master's IPv4 address.

## Protocol version

The stabilized build uses multiplayer protocol **5**. Both executables must be
built from the same current `multiplayer-test` branch. A protocol-4 executable
will intentionally not connect to a protocol-5 executable.

## Expected lobby flow

1. Start both instances and wait for `Peer connected`.
2. Press START on one instance. That machine becomes **Player 1** and proceeds through the normal Music Select / mode selection.
3. The other instance displays the join prompt. Press START there to become **Player 2**.
4. Player 2 stays on the waiting screen. LEFT/RIGHT or the configured gear control changes only Player 2's Ferrari colour. F10 remains a temporary per-race colour change as well.
5. When Player 1 finishes selection, Player 2 receives Player 1's race setup and both instances stop at the pre-race barrier.
6. The console should show a shared scheduled start and both instances should enter the Ferrari intro/countdown together.

Useful console lines include:

```text
[Multiplayer] Protocol v5 enabled as MASTER ...
[Multiplayer] Peer connected
[Multiplayer] Player 1 opened join window for 15 seconds
[Multiplayer] Player 2 joined
[Multiplayer] Player 2 applied Player 1 race setup mode=... region=... level=... colour=...
[Multiplayer] Both players ready. Synchronized start in 1000 ms
[Multiplayer] Synchronized race start mode=... region=... level=...
```

The final two setup/start lines are deliberately verbose for this test. Their
`mode`, `region` and `level` values should agree on both machines.

## What to test now

The priority is stability, not extra features:

- Player 1 must no longer crash when the race initializes.
- Both machines must start the same opening course / Time Trial level.
- Player 2's selected colour must survive the setup handoff and appear in-race.
- The synchronized countdown should remain as tight as the previous build.
- Once `GS_INGAME` begins, each machine should see the other Ferrari and the two cars should not be forced on top of one another.

Do not judge traffic synchronization in this build. Each instance intentionally
runs normal local OutRun traffic again. Shared traffic will return only after the
core two-player race start is stable.
