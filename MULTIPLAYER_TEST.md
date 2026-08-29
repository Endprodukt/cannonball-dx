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

The race leader sends the values that define the initial shared race world:

- Original / Continuous / Endless / Time Trial engine mode
- World or Japanese course mapping
- prototype-course flag
- Time Trial level, traffic and lap count
- selected music
- custom traffic level
- timer/freeze state and timer difficulty
- traffic difficulty
- synchronized launch token/time

The random generator is reset on both instances at the synchronized launch, so
both begin the race with the same random sequence and the same deterministic
Stage 1 traffic initialization.

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

## Important traffic status

The synchronized race setup and RNG now make the **initial** traffic deterministic,
but this is not yet the final shared-traffic implementation. OutRun's traffic AI
reacts to the local player's position, and collisions can also modify its state.
Therefore the next traffic milestone is a Player-1-authoritative traffic world:
Player 1 will own spawn/lane/speed state for the eight traffic slots and Player 2
will receive that state instead of independently evolving a second traffic world.

Do not treat the current prototype as final traffic synchronization yet.

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

The protocol version for this lobby build is **3**, so both executables must be
built from the same current branch.

### Expected test

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
Finish the mode/music selection on Player 1. The consoles should then report that
Player 2 applied Player 1's race setup, followed by a synchronized start message.
The Ferrari intro and countdown should begin together.

Repeat the test with the network slave pressing START first. That instance must
still become Player 1 / Race Leader; the network master should become Player 2
when it joins.

## LAN test

For LAN use, set the slave `host` to the master's IPv4 address and allow inbound
UDP `51337` on the network master if the firewall requires it.

## Next milestones

1. Player-1-authoritative traffic state and Player-2 collision events.
2. Authoritative fork choice: whichever player reaches the branch decision first
   locks the route for both instances.
3. Shared race tick drift detection/correction.
4. Shared finish/game-over rules and multiplayer result screen.
5. Player-to-player collision and network interpolation/lag compensation.
