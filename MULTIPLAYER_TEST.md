# CannonBall DX - Two Player Multiplayer Prototype

This branch contains the experimental two-player networking work. Each CannonBall
instance is still authoritative for its own driving physics, but the two programs
now establish a persistent UDP connection before the race and use a master-driven
start barrier so the actual OutRun race initialization begins together.

## Implemented in the current prototype

- Two CannonBall instances exchange state over UDP.
- Master and slave can run from two independent folders/configurations.
- Networking stays active in menus, Attract Mode, Music Select and gameplay.
- When one player starts a race, `GS_INIT_GAME` is held until the second player
  has also reached the same ready point.
- The master then schedules the launch slightly in the future (default 1000 ms).
- Both instances release the engine at that launch point, so road initialization,
  traffic initialization, Ferrari intro and countdown start together.
- The OutRun random generator is reset at synchronized launch so different
  amounts of time spent in Attract Mode do not immediately produce different
  random traffic streams.
- The remote Ferrari is rendered as a real perspective-correct OutRun sprite.
- Lateral position, steering, speed, stage and road position are exchanged.
- The peer Ferrari is drawn only during `GS_INGAME`. It is deliberately not
  mixed into the scripted `GS_START1/2/3` Ferrari animation anymore.
- Master and slave use separate logical starting-lane origins so equal local
  `car_x_pos` values do not place both cars directly on top of each other.

Not implemented yet:

- authoritative route selection at forks
- synchronization/forcing of the selected game mode or Time Trial course
- player-to-player collision
- fully authoritative shared traffic state
- shared timer/score/game-over rules
- lag compensation/interpolation
- internet lobby/NAT traversal

## Local two-instance test

Build the `multiplayer-test` branch, then make two complete game folders, for
example:

- `CannonBall-DX-Master`
- `CannonBall-DX-Slave`

Each folder needs its own normal CannonBall files and its own `multiplayer.cfg`.
The supplied `multiplayer.cfg.example` can be copied and renamed.

### Master multiplayer.cfg

```ini
enabled = 1
role = master
host = 127.0.0.1
port = 51337
timeout = 15
start_delay_ms = 1000
```

### Slave multiplayer.cfg

```ini
enabled = 1
role = slave
host = 127.0.0.1
port = 51337
timeout = 15
start_delay_ms = 1000
```

Start the master and slave in either order. The master should show:

```text
[Multiplayer] Prototype enabled as MASTER on UDP port 51337
[Multiplayer] Waiting for slave on UDP 51337
[Multiplayer] Peer connected
```

The slave should show:

```text
[Multiplayer] Prototype enabled as SLAVE on UDP port 51337
[Multiplayer] Connecting to master 127.0.0.1:51337
[Multiplayer] Peer connected
```

Select the same game mode on both instances. It is fine for one player to press
START first: that instance should stop at the pre-race boundary and print:

```text
[Multiplayer] Race ready. Waiting for player 2 (timeout 15s)
```

After the second instance reaches the same point, the master should print:

```text
[Multiplayer] Both players ready. Synchronized start in 1000 ms
```

and the slave should receive the scheduled launch. Both should then print:

```text
[Multiplayer] Synchronized race start
```

The visible Ferrari drive-in/countdown should begin together. The peer Ferrari
appears when the race reaches `GS_INGAME`.

If player 2 does not become ready before `timeout`, the waiting instance releases
the engine and runs that race without multiplayer synchronization. Returning to
Music Select arms multiplayer again for the next race.

## LAN test

The master configuration stays the same. On the slave, replace `127.0.0.1`
with the master's IPv4 address, for example:

```ini
enabled = 1
role = slave
host = 192.168.1.50
port = 51337
timeout = 15
start_delay_ms = 1000
```

Allow inbound UDP port `51337` through the master's firewall if required.
Only the master needs a fixed listening port; the slave uses an automatically
assigned source port and the master learns it from the first valid packet.

## Current synchronization rules

The remote car is only drawn when:

- a peer packet has arrived recently;
- both instances are in `GS_INGAME`;
- both instances selected the same CannonBall game mode;
- both instances currently report the same `stage_lookup_off`.

The stage restriction remains intentional. Until fork selection is synchronized,
a Ferrari from a different route must not be projected onto unrelated road data.

The next logical milestone is master-authoritative fork selection: whichever
player reaches the branch decision first chooses the route and that decision is
then applied to both instances.
