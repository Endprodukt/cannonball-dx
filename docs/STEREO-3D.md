# Stereo 3D (layered stereo)

CannonBall DX can render OutRun in stereoscopic 3D, in the spirit of the
"layered" 3D used by later official 3D re-releases of the game. Scenery, traffic
and the road recede into the screen, the Ferrari and the HUD sit on the screen
plane, and objects closer than the car can come slightly out of the screen.

This is phase 1 of the PC-VR work: the engine renders both eyes and packs them
into one frame. That frame works today on 3D TVs/monitors, with cross-eye free
viewing, and in VR through desktop/cinema apps that offer a 3D side-by-side
screen mode. A native OpenXR big-screen renderer is the next phase.

## Quick start

1. Set the engine resolution to **2x or higher** (each eye is packed at half
   resolution, so 2x keeps full arcade detail per eye).
2. Turn off CRT / NTSC filtering, which would blur the two eyes into each other.
3. Turn it on in the menu under **Settings > Video > Video Enhancements >
   Stereo 3D**, or press **F12** to cycle the output mode and **Shift+F12** to
   cycle the depth. The same page has **3D Depth** and **3D Pop Out**; Enter
   or Right steps forward, Left steps back.

## Modes (`video.stereo`)

| Value | Mode | Use with |
| ----- | ---- | -------- |
| 0 | Off | Normal 2D |
| 1 | Half side-by-side, left eye on the left | 3D TVs, VR desktop/cinema apps in 3D SBS mode |
| 2 | Half side-by-side, eyes swapped | Cross-eye free viewing |
| 3 | Half top-and-bottom, left eye on top | 3D TVs / players that prefer TAB |

## Settings in `config.xml`

| Setting | Range | Default | Meaning |
| ------- | ----- | ------- | ------- |
| `video.stereo` | 0-3 | 0 | Output mode (table above) |
| `video.stereo_depth` | 0-48 | 10 | Separation of the far distance, in native (320-wide) pixels. Higher = deeper. |
| `video.stereo_popout` | 0-200 | 50 | How far in front of the screen objects may come, in % of the car's depth. 0 = nothing pops out. |

Start with the default depth. On a large virtual screen, values around 8-12
feel natural; small physical screens can take more.

## How it works

Every layer of the OutRun video hardware already carries depth information:

- **Scenery and traffic**: their true distance into the scene (the engine's
  `z`, 4 to `0x1FF`). The zoom index can't be used for these, because each
  kind of object derives it differently: small scenery is drawn at a fraction
  of normal zoom, traffic zoom depends on vehicle size, and ground strips such
  as grass, water, sand and stones keep zoom fixed and swap artwork instead.
  Traffic level with the player on the start grid is at `0x1D0`, which is
  taken as the car's plane (the screen plane).
- **The Ferrari**, its shadow and passengers sit on the car's plane.
- **Other sprites** (tyre smoke, crash parts, clouds, logos) use their zoom
  index, relative to the Ferrari's zoom (`0x7F`).
- Each hardware sprite is tagged with its depth in an otherwise unused word of
  its sprite entry.
- **Road**: each scanline selects a pre-rendered road ROM row. The width of the
  road in that row is proportional to inverse depth on flat road, hills and
  splits alike. Widths are measured from the ROM once at start-up.
- **Calibration**: scenery and traffic standing on the road give, every frame,
  pairs of (scanline, zoom). Matching the road's depth to those sprites gives
  the road width at the car's depth automatically, so the road, scenery and car
  line up without hand tuning.
- **Tilemaps**: distant backdrop, placed near infinity while the road is on
  screen.
- **Flat screens**: the course map (after a game and in Time Trial),
  Music Select and menus switch the road's horizon off. Those frames are
  rendered once and shown identically to both eyes, i.e. in flat 2D.
- **Text / HUD**: always on the screen plane.

Each frame is drawn twice with every element shifted horizontally by half of
its stereo separation (left eye left, right eye right for things behind the
screen), then packed. Because the packing happens on palette indices before the
renderer, every existing scaler and display mode keeps working.

## Reporting depth problems

Run with the environment variable `CANNONBALL_STEREO_DEBUG=1` to log the road
ROM row widths at start-up and the calibration every 120 frames, for example:

```
Stereo: road 0 row widths: fit ok (slope ..., intercept ...), row0=... row255=...
Stereo: ref width 365.2 (fallback 380, 14 samples) inv@112=0.21 inv@160=0.58 inv@223=1.03
```

`inv@223` should be close to 1.0 during normal driving (the road just in front
of the car is at the car's depth). If the road and roadside objects appear to
float relative to each other, include these lines when reporting it.

## Known limitations

- Half-resolution packing point-samples each eye (palette indices cannot be
  blended). Use 2x engine resolution or higher.
- Pixel scalers and CRT/NTSC filters run on the packed frame and can bleed a
  pixel or two across the seam between the eyes.
- Sprites are flat cut-outs placed at depth: by design, like other layered 3D
  versions of the game.
