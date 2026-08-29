# CannonBall DX - Two Player Multiplayer Prototype

This branch contains the experimental two-player networking work.

`master` and `slave` are transport roles only: they establish the UDP link.
For every race, the player who presses START first becomes **Player 1 / Race
Leader**, regardless of whether that instance is the network master or slave.

## Current lobby flow

1. Start both instances. They establish the normal UDP connection in Attract Mode.
2. The first player to press START becomes Player 1 and opens a join window.
3. Player 1 continues into the normal CannonBall DX Music Select and chooses the
   shared game mode, course options and music.
4. The other instance stays in Attract Mode and displays `JOIN GAME NOW` with the
   remaining join time.
5. Pressing START there joins the offered race as Player 2. Player 2 does not get
   a separate mode selector.
6. While waiting, Player 2 can change only the local Ferrari colour with
   LEFT/RIGHT, the gear controls, or F10.
7. When Player 1 finishes the selection, the complete shared race setup is sent
   to Player 2. Player 2 is moved directly to `GS_INIT_GAME` with that setup.
8. Both instances wait at the pre-race barrier. The transport master schedules a
   common launch slightly in the future (1000 ms by default).
9. Both engines release together, reset the OutRun random stream, initialize the
   race and run the same START1/START2/START3 countdown.

If nobody joins before the join timer expires, Player 1 continues as single
player when the selection is finished.

## Shared setup currently transferred

Player 1 sends the values that define the initial shared race world:

- Original / Continuous / Endless / Time Trial engine mode
- World or Japanese course mapping
- prototype-course flag
- Time Trial level, traffic and lap count
- selected music
- custom traffic level
- timer/freeze state and timer difficulty
- traffic difficulty
- synchronized launch token/time

The random generator is reset on both instances at synchronized launch.

## Shared traffic authority

Player 1 now owns the common OutRun traffic simulation.

The normal, unchanged `OTraffic` code runs only on Player 1 once authoritative
snapshots are available. After each traffic tick, the world state of all eight
traffic slots is included in the normal UDP state packet:

- enabled/disabled state
- road side
- vehicle type
- Player-1-relative depth
- lane/world X positions
- traffic speed/original speed
- temporary hidden state
- wheel palette phase

Player 2 stops evolving a separate traffic AI world and projects those same
traffic cars through Player 2's local camera/road position. This means traffic
spawn, lane changes and traffic-to-traffic decisions come from one authority
instead of reacting independently to two different Ferraris.

Player 2 can already collide locally with a shared traffic car and receives the
normal skid/speed/sound response. **The effect of a Player 2 collision on the
shared traffic car itself is not yet sent back to Player 1.** That collision-event
round trip is the next traffic step. Until then, Player 1 remains authoritative
and its next snapshot will restore the shared car's position/state.

## Player-local values

These remain individual by design:

- Ferrari colour
- steering / throttle / brake / gear
- driving position and speed
- local controls, FFB and video/view preferences

The remote Ferrari is rendered as a real perspective-correct OutRun sprite after
`GS_INGAME`. It is deliberately not added during the original scripted
START1/START2/START3 Ferrari animation, which avoids the old double-car overlap.
Player 1 and Player 2 use separate logical lane origins.

## Local two-instance test

Build the latest `multiplayer-test` branch and use two complete game folders, for
example:

- `CannonBall-DX-Master`
- `CannonBall-DX-Slave`

Each folder needs its own `multiplayer.cfg`.

### Master

```ini
enabled = 1
role = master
host = 127.0.0.1
port = 51337
timeout = 15
start_delay_ms = 1000
```

### Slave

```ini
enabled = 1
role = slave
host = 127.0.0.1
port = 51337
timeout = 15
start_delay_ms = 1000
```

The protocol version for this build is **4**, so both executables must be built
from the same current branch.

### Expected lobby/start test

After both consoles report `Peer connected`, press START on only one instance.
That instance should enter Music Select. The other screen should remain in
Attract and show approximately:

```text
JOIN GAME NOW
JOIN TIME  15
PRESS START TO JOIN
```

Press START on the second instance. It should remain out of the mode selector and
show:

```text
PLAYER 2 JOINED
CAR COLOR  RED
WAITING FOR PLAYER 1...
```

Change Player 2's colour and verify that it stays selected while waiting.
Finish the mode/music selection on Player 1. The consoles should report that
Player 2 applied Player 1's race setup, followed by a synchronized start message.
The Ferrari intro and countdown should begin together.

Then drive both players at noticeably different speeds/positions and watch the
same traffic cars. The important check is that a traffic car changing lane or a
new car spawning remains the same shared vehicle on both screens rather than the
two instances gradually inventing separate traffic patterns.

Repeat the test with the network slave pressing START first. That instance must
still become Player 1 / Race Leader; the network master should become Player 2
when it joins.

## LAN test

For LAN use, set the slave `host` to the master's IPv4 address and allow inbound
UDP `51337` on the network master if the firewall requires it.

## Next milestones

1. Send Player 2 traffic-collision events back to Player 1 so impacts alter the
   shared traffic world authoritatively.
2. Authoritative fork choice: whichever player reaches the branch decision first
   locks the route for both instances.
3. Shared race tick drift detection/correction.
4. Shared finish/game-over rules and multiplayer result screen.
5. Player-to-player collision and network interpolation/lag compensation.
