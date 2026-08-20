# External Outputs

CannonBall-SE can expose lamp states to arcade-output software using two MAME-compatible transports at the same time:

- **Network output** on TCP port **8000** (MAME network protocol)
- **Windows output messages** (`MAMEOutput*`), for tools such as MAMEHooker

The machine name reported by both transports is **`cannonball`**. This intentionally keeps CannonBall-SE separate from MAME's `outrun` machine and allows a dedicated `cannonball.ini`.

The existing SmartyPi output path is independent and remains unchanged.

## Outputs

| Output | Meaning |
| --- | --- |
| `Start_lamp` | START-button lamp; blinks with PRESS START in attract mode and stays on while the game is active |
| `Brake_lamp` | Original OutRun brake lamp |
| `View_lamp` | Single VIEW-button lamp; steadily on while the driving sequence/game is active |
| `View1_lamp` | On while the original camera view is selected |
| `View2_lamp` | On while the elevated camera view is selected |
| `View3_lamp` | On while the in-car/bumper camera view is selected |

### START lamp behaviour

- Attract screens showing **PRESS START**: blinks in the same phase as the on-screen text
- Music selection: off
- Car driving in, countdown, race and bonus sequence: steadily on
- Game over / course map: off
- When attract mode resumes and **PRESS START** is shown again: blinks again

This external `Start_lamp` behaviour is intentionally independent of the original OutRun start-lamp bit. This is necessary because CannonBall's freeplay PRESS START display does not drive the original hardware bit.

`View_lamp` does not blink. It switches on when the car-driving-in sequence starts and switches off when game-over begins.

## Optional config.xml settings

Network and Windows output are enabled by default. These optional settings can be added as a top-level block to change that behaviour:

```xml
<outputs>
    <network>1</network>
    <windows>1</windows>
    <port>8000</port>
</outputs>
```

On non-Windows platforms the Windows output setting is ignored.

## View controls

The existing `view` binding is unchanged and continues to cycle through all three views with one button.

The normal **Configure Controls** wizard now continues after `PRESS VIEW CHANGE` with three optional bindings:

- `PRESS VIEW 1` - directly selects the original camera
- `PRESS VIEW 2` - directly selects the elevated camera
- `PRESS VIEW 3` - directly selects the in-car/bumper camera

Each can be assigned to a keyboard key or joystick/wheel/gamepad button. Press **Enter** to skip an optional direct-view binding. The cyclic VIEW button and the three direct buttons can coexist.

The bindings are stored in `config.xml` as:

```xml
<keyconfig>
    <view>118</view>
    <view1>-1</view1>
    <view2>-1</view2>
    <view3>-1</view3>
</keyconfig>

<padconfig>
    <view>3</view>
    <view1>-1</view1>
    <view2>-1</view2>
    <view3>-1</view3>
</padconfig>
```

`-1` means unassigned.
