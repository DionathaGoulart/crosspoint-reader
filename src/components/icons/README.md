Lucide and Feather icons keep their original licenses.
The licenses and copyright notices are in `freeink-sdk/libs/assets/Icons/lucide/LICENSE` at the repository root.
Keep those notices with copies of those icons, including converted bitmaps.

The home icons (`folder`, `library`, `stats`, `blocks`, `transfer`, `settings2`, `book`) are 8-bit pixel art.
`scripts/gen_pixel_icons.py` draws them on a 16x16 grid and writes the 32x32 headers.
Edit the drawing functions there, then run the script; do not edit these headers by hand.

The other icon headers use the `freeink::Icon` format. The SDK's `freeink-sdk/libs/assets/Icons/tools/gen_icons.py`
makes them from Lucide icons, and `customListIcons.h` is kept by hand. `scripts/pixelate_icons.py` snaps all of them to
2x2 pixel blocks, so they match the home icons. It finds the headers by itself.
After regenerating any of them, run `python3 scripts/pixelate_icons.py`, then `./bin/clang-format-fix -g`.
`python3 scripts/pixelate_icons.py --check` fails when a header is not pixelated, or when an icon array has no
matching `freeink::Icon` line.
