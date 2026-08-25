# Third-Party Notices

This project includes or interfaces with third-party components. Copies of their
licenses are included where required.

---

## Blargg SNES NTSC Video Filter

- **Component:** `snes_ntsc`
- **Author:** Shay Green ('Blargg') <gblargg@gmail.com>
- **License:** GNU Lesser General Public License (LGPL) v2.1
- **License text:** `docs/LGPL-2.1.txt`
- **Docs:** `docs/Blargg-NTSC-Filter-Concepts-and-Implementation.txt`

### Notes

This library provides composite-video style NTSC filtering. Preserve its copyright
and license notices when redistributing CannonBall DX.

---

## xBRZ

- **Component:** `xBRZ`
- **Author:** Zenju
- **Copyright:** Copyright (C) Zenju
- **License:** GNU General Public License v3 with the exception text contained in the upstream HqMAME-derived source
- **Source used by CannonBall DX:** pinned Mesen2 snapshot fetched at build time
- **Upstream source:** `SourMesen/Mesen2`, `Utilities/xBRZ`

### Notes

CannonBall DX uses xBRZ for its 3x, 4x, 5x and 6x pixel-scaling modes. The build
fetches the scaler source from a pinned Mesen2 revision and retains the upstream
copyright and license headers.

---

## HQx

- **Component:** `HQx`
- **Authors / Copyright:** Copyright (C) 2003 Maxim Stepin; Copyright (C) 2010 Cameron Zemek
- **License:** GNU Lesser General Public License v2.1 or later
- **License text:** `docs/LGPL-2.1.txt`
- **Source used by CannonBall DX:** pinned Mesen2 snapshot fetched at build time
- **Upstream source:** `SourMesen/Mesen2`, `Utilities/HQX`

### Notes

CannonBall DX uses HQx for its 3x and 4x pixel-scaling modes. The build fetches
the scaler source from the same pinned Mesen2 revision used for xBRZ and retains
the upstream copyright and license headers.

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
