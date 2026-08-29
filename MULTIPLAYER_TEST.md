# CannonBall DX - Two Player Multiplayer Test

This branch contains the current small, testable multiplayer core. The older
shared-traffic experiment is preserved on `multiplayer-test-v4-backup`; the
state immediately before the dedicated grid-start work is preserved on
`multiplayer-test-pre-grid-start`.

## Current test scope

Implemented:

- two CannonBall instances over UDP;
- transport `master` / `slave` roles;
- the first player to press START becomes Player 1 / race leader;
- Player 2 can join during the timeout window;
- Player 1 owns game mode, World/JP course mapping, Time Trial level, music and difficulty;
- Player 2 owns an independent Ferrari colour;
- both instances wait at `GS_INIT_GAME` and launch from a shared start token;
- course mapping, Stage 1 identity, road position and RNG are reset immediately before launch;
- multiplayer skips the original Ferrari drive-in completely;
- both Ferraris use their normal road sprites from `GS_START1` onward and are visible together on the grid;
- the unused 50-tick drive-in delay is removed, leaving three equal countdown phases before GO;
- the selected music is reset and restarted on the first shared countdown frame on both instances;
- remote longitudinal position uses an OutRun/traffic-style perspective mapping rather than the old linear approximation;
- the logical visual grid spacing is reduced so equal-position cars appear side by side instead of far apart;
- on Windows, only the first local CannonBall DX process claims the physical FFB device, avoiding two processes opening the same wheel.

Not active yet:

- shared traffic authority;
- player-to-player collision;
- authoritative fork choice;
- timer/score/game-over synchronization;
- packet interpolation / latency compensation;
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

The grid-start build uses multiplayer protocol **6**. Both executables must be
built from the same current `multiplayer-test` branch. Older protocol versions
will intentionally not connect.

## Expected lobby and grid flow

1. Start both instances and wait for `Peer connected`.
2. Press START on one instance. That machine becomes **Player 1** and proceeds through the normal Music Select / mode selection.
3. The other instance displays the join prompt. Press START there to become **Player 2**.
4. Player 2 stays on the waiting screen. LEFT/RIGHT or the configured gear control changes only Player 2's Ferrari colour. F10 remains a temporary per-race colour change as well.
5. When Player 1 finishes selection, Player 2 receives Player 1's race setup and both instances stop at the pre-race barrier.
6. The shared launch token releases both engines together.
7. The normal single-player Ferrari drive-in is skipped. Both Ferraris should already be visible on the start line.
8. START1, START2 and START3 are three equal countdown phases, followed by GO / `GS_INGAME`.
9. The selected music is hard-restarted on the first shared countdown frame on both machines.

Useful console lines include:

```text
[Multiplayer] Protocol v6 enabled as MASTER ...
[Multiplayer] Peer connected
[Multiplayer] Player 1 opened join window for 15 seconds
[Multiplayer] Player 2 joined
[Multiplayer] Player 2 applied Player 1 race setup mode=... region=... level=... music=... colour=...
[Multiplayer] Both players ready. Synchronized grid in 1000 ms
[Multiplayer] Synchronized grid start mode=... region=... level=... music=...
[Multiplayer] Grid start: Ferrari intro skipped
[Multiplayer] Grid music synchronized: track ...
```

The `mode`, `region`, `level` and `music` values should agree between the two
machines.

## What to test now

- Both machines must enter the same course and countdown at the same time.
- Neither Ferrari should perform the original drive-in animation.
- Both cars should already be visible side by side before GO.
- Player 2's selected colour must survive the setup handoff.
- The same music track should restart at the same point on both instances.
- If one Ferrari remains stationary while the other drives past it, the stationary car should approach naturally in perspective and then disappear behind the local camera instead of appearing to travel along with it.
- The remote car should no longer sit an excessive distance to the side when both road positions are equal.

Traffic remains local to each instance in this build. Shared traffic should only
return after the two-player start, car projection and route state are stable.
