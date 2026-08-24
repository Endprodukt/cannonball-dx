# Third-Party Notices

This project includes or interfaces with third-party components. Copies of their
licenses are included where required.

---

## Blargg SNES NTSC Video Filter

- **Component:** `snes_ntsc`
- **Author:** Shay Green ('Blargg') <gblargg@gmail.com>
- **License:** GNU Lesser General Public License (LGPL) — see `licenses/LGPL-2.1.txt`
- **Docs:** `docs/snes_ntsc.txt`

### Notes

This library provides composite-video style NTSC filtering. When statically linking
LGPL code into binaries, ensure recipients can relink with a modified version (for
example, by providing relinkable object files); when dynamically linking, ensure users
can swap in a compatible modified library. Preserve copyright and license notices.

For the full terms, see `licenses/LGPL-2.1.txt`.

---

## miniz

- **Component:** `miniz` 3.1.2
- **Original author:** Rich Geldreich
- **License:** Public domain / Unlicense
- **Upstream:** `richgel999/miniz`

### Notes

CannonBall DX uses miniz for read-only ZIP archive access so OutRun ROMs can be
loaded directly from MAME ZIP sets. The dependency is fetched at build time from a
pinned upstream revision. miniz permits unrestricted use under its public-domain /
Unlicense terms.
