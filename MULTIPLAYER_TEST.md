# CannonBall DX - Two Player Multiplayer Prototype

This branch contains the first deliberately small multiplayer experiment.
It does **not** synchronize the full game yet. Each CannonBall instance still
runs its own physics and game state. The prototype exchanges enough live state
to render the other player's Ferrari in the local OutRun road scene.

## What this test proves

- Two CannonBall instances can discover/exchange state over UDP.
- The master and slave can run from two independent folders/configurations.
- The remote Ferrari can be rendered as a real perspective-correct OutRun sprite.
- Lateral position, steering, speed, stage and road position are exchanged.
- A disconnected peer times out after about 1.5 seconds.

Not implemented yet:

- synchronized countdown/start
- authoritative route selection at forks
- player-to-player collision
- shared traffic state
- shared timer/score/game-over
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
```

### Slave multiplayer.cfg

```ini
enabled = 1
role = slave
host = 127.0.0.1
port = 51337
```

Start the master first, then the slave. The console should show messages similar
to:

```text
[Multiplayer] Prototype enabled as MASTER on UDP port 51337
[Multiplayer] Waiting for slave on UDP 51337
[Multiplayer] Peer connected
```

and on the slave:

```text
[Multiplayer] Prototype enabled as SLAVE on UDP port 51337
[Multiplayer] Connecting to master 127.0.0.1:51337
[Multiplayer] Peer connected
```

Then start the same normal game mode in both instances. During the start/race,
the other Ferrari should appear in the same road scene. For this first test the
master has a small conceptual left-lane offset and the slave a small right-lane
offset so two cars at identical physical `car_x_pos` do not render on top of one
another.

## LAN test

The master configuration stays the same. On the slave, replace `127.0.0.1`
with the master's IPv4 address, for example:

```ini
enabled = 1
role = slave
host = 192.168.1.50
port = 51337
```

Allow inbound UDP port `51337` through the master's firewall if required.
Only the master needs a fixed listening port; the slave uses an automatically
assigned source port and the master learns it from the first valid packet.

## Current synchronization rules

The remote car is only drawn when:

- a peer packet has arrived recently;
- both instances are in the start/race state;
- both instances selected the same CannonBall game mode;
- both instances currently report the same `stage_lookup_off`.

That last restriction is intentional. Until route selection is synchronized,
a car from a different branch must not be projected onto unrelated local road
data.

## Recommended first test

For the cleanest initial result, launch the same mode on both instances within a
few seconds of one another and drive the opening stage without deliberately
separating the game states. The first thing to verify is not perfect race logic;
it is that steering one instance visibly moves/turns the second Ferrari in the
other instance and vice versa.

If that works, the next logical milestone is master-authoritative start and fork
selection, followed by interpolation and player collision.
