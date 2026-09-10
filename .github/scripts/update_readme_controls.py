from pathlib import Path


def update_readme_txt():
    path = Path("README.txt")
    text = path.read_text()
    start = text.index("-------------------------------------\n4. Controls & Fixed Keyboard Hotkeys\n-------------------------------------\n")
    end = text.index("-------------------------------------\n5. Bugs & Development\n-------------------------------------\n", start)

    replacement = """-------------------------------------
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

"""
    path.write_text(text[:start] + replacement + text[end:])


def update_readme_md():
    path = Path("README.md")
    text = path.read_text()
    start = text.index("### Default Keyboard Controls\n")
    end = text.index("### Force Feedback settings\n", start)

    replacement = """### Default Keyboard Controls

- **Steer:** Left / Right arrows
- **Accelerate:** `Left Ctrl`
- **Brake:** `Left Alt`
- **Low / High Gear:** `Space` / `Left Shift`
- **Start:** `1`
- **Coin:** `5`
- **Change View:** `Z`
- **Radio:** `X`
- **Menu Access:** `Tab`
- **Pause:** `P`
- **Menu Accept:** `Enter`
- **Menu Back:** `Esc`
- **Direct View 1 / 2 / 3:** Unassigned

### Default Gamepad Controls

- **Steer:** Left Stick
- **Accelerate:** Right Trigger / R2
- **Brake:** Left Trigger / L2
- **Low Gear:** A / Cross
- **High Gear:** X / Square
- **Start:** Start / Options
- **Coin:** Back / Select / Create
- **Change View:** Y / Triangle
- **Radio:** L3
- **Menu Access:** Guide / Xbox / PS button
- **Pause:** R3
- **Menu Accept:** A / Cross
- **Menu Back:** B / Circle
- **Menu Navigation:** D-Pad

### Fixed Keyboard Controls

- **Arrow Keys** Menu navigation
- **Enter** Menu Accept
- **Esc** Menu Back / Quit during gameplay
- **F2** Frame step
- **F3** Freeze timer
- **F5** Menu Access
- **F6** Pixel-scaler quick cycle
- **F7** Hi-res sprites
- **F8** Video-processing toggle
- **F9** Shadow-mask toggle
- **F10** Default Ferrari color
- **F11** Windowed / Fullscreen
- **Alt+Enter** Windowed / Fullscreen

"""
    path.write_text(text[:start] + replacement + text[end:])


update_readme_txt()
update_readme_md()
