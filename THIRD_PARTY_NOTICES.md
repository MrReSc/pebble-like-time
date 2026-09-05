# Third-party notices

## PebbleOS artwork

The status bitmaps derive from [Core Devices PebbleOS](https://github.com/coredevices/PebbleOS/tree/70ed290ff9b134b492340bad7018946ec38ea0d0),
commit `70ed290ff9b134b492340bad7018946ec38ea0d0`, under [Apache-2.0](LICENSE).
PebbleOS contains work copyright Google LLC and Core Devices LLC.

| Local file in `resources/images/` | Upstream source under `resources/normal/base/images/` |
| --- | --- |
| `heart.png` | `Pebble_25x25_Heart.svg` |
| `sunrise.png` | `Pebble_25x25_Sunrise.svg` |
| `sunset.png` | `Pebble_25x25_Sunset.svg` |
| `calendar.png` | `Pebble_25x25_Calendar.svg` |
| `steps.png` | `Pebble_25x25_Run.svg`, `stride_shoe_blue.png` (geometry references) |

Modifications by MrReSc, 2026: rasterization, adapted geometry, monochrome black
details, white interiors and transparent outer backgrounds.

The C battery drawing uses proportions from `tools/bitmaps/status_battery_empty.png`.
The dotted separators follow the pixel pattern in
`src/fw/apps/system/workout/active.c` at the same upstream commit.
System fonts are referenced through PebbleOS APIs; no font files are bundled.

## SunCalc 1.9.0

`src/pkjs/vendor/suncalc.js` is an unmodified copy from
[SunCalc v1.9.0](https://github.com/mourner/suncalc/tree/v1.9.0), commit
`f6fe07c430af198f7c42788f01989a22a1269b08`.

Copyright (c) 2014, Vladimir Agafonkin. All rights reserved.
The source also retains its `(c) 2011-2015, Vladimir Agafonkin` header.
The complete BSD-2-Clause license is in
[LICENSES/SunCalc-BSD-2-Clause.txt](LICENSES/SunCalc-BSD-2-Clause.txt).

## Original work and distribution

Original application code, documentation and `resources/images/menu_icon.png`:
Copyright 2026 MrReSc, [Apache-2.0](LICENSE). Store images depict the application
and the attributed artwork above. Retain the full licenses and these notices
when distributing source or binaries; they are included in the PBW.

The optional `.agents/` skill submodule retains its own upstream terms and is
not included in the PBW. The application license does not relicense it.
