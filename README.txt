=====================================
CannonBall DX - Windows Build
=====================================

A modern arcade-cabinet and racing-wheel focused fork of CannonBall-SE,
based on Chris White's CannonBall OutRun engine.

DX Fork / Maintainer: Endprodukt
Repository: https://github.com/Endprodukt/cannonball-dx
Branch: master
Tested Platform: Windows 10/11 (64-bit)

IMPORTANT PLATFORM NOTE
-----------------------
CannonBall DX is currently developed and tested ONLY on Windows 10/11 (64-bit).

Linux, Raspberry Pi and other platforms have NOT been tested with the DX changes.
The upstream CannonBall and CannonBall-SE projects support additional platforms,
and some of that platform-specific code remains present, but this does not mean
that CannonBall DX or its added features work correctly on those systems.

No compatibility outside Windows is claimed or guaranteed.

Development of the additional CannonBall DX work was carried out with assistance
from ChatGPT by OpenAI and Claude by Anthropic.

-------------------------------------
1. About CannonBall DX
-------------------------------------

CannonBall DX builds directly on:

- CannonBall, created by Chris White
- CannonBall-SE, created and maintained by James Pearce (J1mbo)

The goal of CannonBall DX is to retain the original OutRun feel while extending
the project for dedicated arcade cabinets, modern racing wheels and flexible
multi-device PC setups.

Key features available in CannonBall DX include:

- Original, Original Japanese, Continuous, Endless and Time Trial game modes
- Game-mode selection on the Music Select Screen using the VIEW controls
- True multi-device input for wheel, pedals, shifter and buttons
- Separate Keyboard, Gamepad and Wheel bindings
- Persistent device identification
- Improved keyboard, D-Pad and high-score-entry navigation
- Expanded steering-wheel force feedback with configurable per-effect tuning
- Configurable RPM-linked engine vibration
- Gamepad rumble
- Improved CannonBall-SE 2x Game Engine Resolution plus new 3x and 4x modes
- 30 FPS, Original timing, 60 FPS and experimental 120 FPS modes
- 4:3, 16:9, 16:10, 21:9 and Stretched aspect ratios
- Windowed, Borderless Fullscreen and experimental Exclusive Fullscreen modes
- xBRZ and HQX pixel scaling
- Enhanced Attract Mode
- Eight selectable Ferrari colours
- Dedicated Time Trial and Endless records / results
- Assignable in-game Radio control for changing music while driving
- Individually configurable gameplay bug-fix options
- START, BRAKE, VIEW, VIEW1, VIEW2, VIEW3 and RADIO cabinet outputs
- MAME network output, Windows MAMEOutput / MAMEHooker and SmartyPi output
- Direct MAME ROM ZIP loading

CannonBall already contained steering-wheel force feedback. CannonBall DX extends
that existing implementation with additional effects and modern wheel handling;
it is not a replacement for a rumble-only system.

The DX force-feedback extensions also include configurable engine vibration whose
cadence follows engine RPM, alongside separate start-rev and driving behaviour.

-------------------------------------
2. ROMs - Required
-------------------------------------

ROM files are NOT included with CannonBall DX.

CannonBall DX requires original Sega OutRun Revision B ROM data. You are
responsible for obtaining and using ROM data legally. You should own the original
game or otherwise have the legal right to use the required ROM images according
to the laws applicable in your country.

Recommended setup:

Place a suitable current MAME merged archive named, for example:

outrun.zip

inside the roms directory. The archive does not need to be extracted.

CannonBall DX identifies ZIP entries using CRC32 and uncompressed size rather than
relying only on archive filenames. This allows compatible Revision B data,
alternate early/Japanese program ROMs and corrected PCM data to be located inside
modern merged MAME archives even when MAME uses different filenames or paths.

Traditional extracted CannonBall ROM files remain fully supported. Loose ROM
files take priority when identical data is also available inside a ZIP archive.

See:

roms\roms.txt

for the traditional filenames and accepted ROM variants.

Original Japanese / Early Version
---------------------------------
CannonBall DX can use the alternate early/Japanese OutRun program ROMs to provide
the alternate Japanese/early course configuration. A suitable merged outrun.zip
may already contain these files; DX locates them automatically by CRC32 and size.

Corrected PCM Sample ROM
------------------------
The original OutRun PCM sample ROM contains a known stuck-data-bit fault.
When fixed samples are enabled, CannonBall DX accepts compatible data in this
order:

1. Sega/M2 official corrected ROM
   CRC32: C2DE09B2

2. Historical CannonBall patched ROM
   Filename: opr-10188.71f
   CRC32: 37598616

3. Original arcade ROM as backwards-compatible fallback
   Filename: opr-10188.71
   CRC32: BAD30AD9

No manual PCM patch is required when the official corrected Sega/M2 ROM is
available.

-------------------------------------
3. Installation & Usage
-------------------------------------

1. Extract the complete CannonBall DX Windows release into one directory.
2. Place either a suitable merged outrun.zip or the traditional extracted
   OutRun Revision B ROM files inside the roms directory.
3. Run CannonBall-DX.exe.
4. Do not remove required DLL files or files from the res directory.

The Microsoft Visual C++ Redistributable may be required on Windows systems.

CannonBall DX is tested only on Windows 10/11 64-bit. Other operating systems are
not part of the tested DX release target.

-------------------------------------
4. Controls & Fixed Keyboard Hotkeys
-------------------------------------

Controls are configured through:

Menu -> Settings -> Controls

The binding editor provides separate Keyboard, Gamepad and Wheel columns.

Default keyboard controls:

- Steer: Left / Right arrows
- Accelerate: Left Ctrl
- Brake: Left Alt
- Low / High Gear: Space / Left Shift
- Start: 1
- Coin: 5
- Change View: Z
- Radio: X
- Menu Access: Tab
- Pause: P
- Menu Accept: Enter
- Menu Back: Esc
- Direct View 1 / 2 / 3: Unassigned

Default gamepad controls:

- Steer: Left Stick
- Accelerate: Right Trigger / R2
- Brake: Left Trigger / L2
- Low Gear: A / Cross
- High Gear: X / Square
- Start: Start / Options
- Coin: Back / Select / Create
- Change View: Y / Triangle
- Radio: L3
- Menu Access: Guide / Xbox / PS button
- Pause: R3
- Menu Accept: A / Cross
- Menu Back: B / Circle
- Menu Navigation: D-Pad

Fixed keyboard controls:

- Arrow Keys  Menu navigation
- Enter       Menu Accept
- Esc         Menu Back / Quit during gameplay
- F2          Frame step
- F3          Freeze timer
- F5          Menu Access
- F6          Pixel-scaler quick cycle
- F7          Hi-res sprites
- F8          Video-processing toggle
- F9          Shadow-mask toggle
- F10         Default Ferrari color
- F11         Windowed / Fullscreen
- Alt+Enter   Windowed / Fullscreen

Menu and digital-control improvements:

- Keyboard and D-Pad navigation has been improved throughout the frontend.
- High-score initials can be selected directly with Left / Right in Original,
  Time Trial and Endless modes.
- Start or Menu Accept can be used to confirm high-score entries.
- Time Trial course selection can be navigated in two dimensions with the
  keyboard or D-Pad, following the course map.
- Analog steering / wheel-based Time Trial selection remains supported.

In-game Radio control:

- The Radio action can be assigned independently for Keyboard, Gamepad and Wheel.
- While driving, Radio cycles through the available music tracks and Music Off.
- The selected track is briefly shown on screen.
- A dedicated Radio_lamp output is available for cabinet setups.

-------------------------------------
5. Experimental Features & Known Limitations
-------------------------------------

The following features should currently be considered experimental:

- 3x and 4x Game Engine Resolution
- 120 FPS mode
- Exclusive Fullscreen

CannonBall-SE already provided a 2x high-resolution rendering mode. CannonBall DX
builds on that work with additional rendering fixes and the new 3x and 4x modes.

Higher internal resolutions make details in the original OutRun graphics much
easier to see. As a result, behaviour that was difficult to notice at the
original 320x224 resolution can become more obvious, including:

- distant cars using different or simplified sprite frames
- visible transitions between sprite sizes or viewing angles
- small road, shadow or scenery alignment differences
- limitations inherited from the original arcade graphics

Some of these issues can be improved in CannonBall DX and development is
continuing. Others are simply part of how the original arcade game works.

If you find a crash, graphical problem, input issue or behaviour that differs
from the original arcade version, please open a GitHub Issue. Screenshots,
videos, reproduction steps and comparisons with the arcade version are especially
helpful.

-------------------------------------
6. Bugs & Development
-------------------------------------

For source code, development information, bugs and feature requests:

https://github.com/Endprodukt/cannonball-dx

The actively developed branch is master.

-------------------------------------
7. Build & Source Information
-------------------------------------

CannonBall DX is developed and tested on Windows 10/11 (64-bit).
Windows build instructions are provided in:

docs\Compiling-On-Windows.txt

CannonBall DX is a modified derivative of CannonBall and CannonBall-SE.

The original CannonBall license requires modified redistributions to include the
complete source code required by that license. Binary CannonBall DX distributions
must therefore be accompanied by the corresponding source code as required by the
applicable upstream terms.

Source repository:

https://github.com/Endprodukt/cannonball-dx

-------------------------------------
8. Credits
-------------------------------------

CannonBall DX builds directly on the work of the original projects and their
contributors.

Chris White
-----------
Creator of CannonBall and the core C++ recreation of Sega OutRun.
Copyright is held by Chris White and the CannonBall team as described in:

docs\license.txt

James Pearce (J1mbo)
---------------------
Creator and maintainer of CannonBall-SE, including its cabinet, video,
performance, audio and gameplay enhancements.

CannonBall-SE enhancements and original contributions are Copyright © 2020-2025
James Pearce.

See:

docs\CannonBall-SE-license.txt

Endprodukt
----------
CannonBall DX fork, feature direction, testing and DX-specific development,
including work relating to modern wheel support, expanded force feedback,
multi-device controls, additional game modes, ultrawide presentation, cabinet
outputs and presentation features.

Development assistance for the additional CannonBall DX work was provided by:

ChatGPT by OpenAI
Claude by Anthropic

Additional contributors / components
------------------------------------

Shay Green (Blargg)
Author of the snes_ntsc video filter used for NTSC/composite-style processing.
The component is licensed under the GNU Lesser General Public License v2.1.

Zenju
Author of xBRZ, used for CannonBall DX 3x, 4x, 5x and 6x pixel-scaling modes.
The HqMAME-derived implementation used by DX is obtained from the pinned Mesen2
source snapshot used by the build.

Maxim Stepin and Cameron Zemek
Authors/contributors of the HQx implementation used for CannonBall DX 3x and 4x
pixel-scaling modes. HQx is licensed under the GNU Lesser General Public License
v2.1 or later.

SourMesen / Mesen2 contributors
Mesen2 provides the pinned HqMAME-derived xBRZ and HQx source implementations
used by the CannonBall DX build.

Rich Geldreich and miniz contributors
Authors and contributors to miniz, used by CannonBall DX for direct ZIP ROM
loading. miniz is provided under its public-domain / Unlicense terms.

rtissera
RISC-V RVV 1.0 SIMD support and x86 SSE2 fallback inherited through
CannonBall-SE.

David Firth and the Atari800 development team
Inherited Atari800-derived code is covered by the license in:

docs\license_atari800.txt

MAMEDev and contributors
Inherited MAME-derived code is covered by the license in:

docs\license_mame.txt

Further third-party information is available in:

docs\THIRD-PARTY-NOTICES.md

-------------------------------------
9. Third-Party Libraries
-------------------------------------

CannonBall DX uses or interfaces with open-source components including SDL2,
TinyXML2, mpg123, ANGLE, zlib, snes_ntsc, xBRZ, HQx and miniz, depending on
platform and build configuration.

xBRZ
----
Copyright © Zenju.
The HqMAME-derived implementation used by CannonBall DX is distributed under the
GNU General Public License v3 with the exception text retained in the upstream
source.

HQx
---
Copyright © 2003 Maxim Stepin and © 2010 Cameron Zemek.
Licensed under the GNU Lesser General Public License v2.1 or later.

Each component remains subject to its respective copyright and license terms.
Relevant license and notice files are supplied with the project where applicable.
Copyright and license notices must not be removed when redistributing CannonBall
DX.

See:

docs\THIRD-PARTY-NOTICES.md

-------------------------------------
10. License
-------------------------------------

CannonBall DX remains subject to all applicable upstream license conditions.

Original CannonBall license
---------------------------
The CannonBall license includes conditions that:

- Redistributions may not be sold.
- The software may not be used in a commercial product or activity.
- Modified redistributions must include the complete source code required by
  the license.
- Existing copyright notices and license terms must be preserved.
- The software is provided without warranty.

The complete controlling terms are contained in:

docs\license.txt

CannonBall-SE additional terms
------------------------------
The additional CannonBall-SE terms also apply to CannonBall-SE-derived portions
of this project. These include:

- CannonBall-SE enhancements © 2020-2025 James Pearce.
- The software is provided "as is".
- CannonBall-SE may not be sold or otherwise monetised.
- Original CannonBall and CannonBall-SE notices must be preserved.

See:

docs\CannonBall-SE-license.txt

DX-specific modifications do not grant any additional commercial rights beyond
those permitted by the applicable upstream licenses.

Third-party components, including xBRZ and HQx, remain licensed under their own
respective terms as documented in docs\THIRD-PARTY-NOTICES.md.

-------------------------------------
11. Sega / OutRun Notice
-------------------------------------

OutRun is a trademark of SEGA Corporation.

CannonBall DX is an independent fan-developed project and is not produced,
licensed, endorsed or affiliated with SEGA Corporation.

No Sega ROM data is distributed with CannonBall DX.

-------------------------------------
12. Warranty
-------------------------------------

This software is provided without warranty.

Any and all use of CannonBall DX is entirely at your own risk.

For the legally controlling terms, refer to the license files supplied with the
project.

CannonBall DX
DX fork and additional development: Endprodukt
Based on CannonBall-SE by James Pearce (J1mbo)
Based on CannonBall by Chris White
Developed with assistance from ChatGPT by OpenAI and Claude by Anthropic

Copyright notices belonging to CannonBall, CannonBall-SE and all third-party
components remain with their respective copyright holders.
