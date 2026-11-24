# Host simulator

Builds this firmware as a native program and draws the panel in an SDL2 window,
so screens and navigation can be checked without flashing. Upstream project:
[crosspoint-reader/crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator) (MIT).

It is a development target only. `platform = native` shares no object files with
any device environment, so it costs the firmware nothing in flash or RAM.

## Setup

The flags in `platformio.ini` are the upstream macOS sample's
(`-D_LIBCPP_DISABLE_AVAILABILITY`, `-Wno-c++11-narrowing`). On Linux or WSL,
install `libsdl2-dev` and `libssl-dev` instead of the `brew` line and add
`-lssl -lcrypto` to the environment's `build_flags`, as the upstream
`sample-platformio-linux-wsl.ini` does.

```sh
brew install sdl2
git clone https://github.com/crosspoint-reader/crosspoint-simulator.git ../crosspoint-simulator
git -C ../crosspoint-simulator apply "$PWD/sim/simulator-shims.patch"
pio run -e simulator_x4_classic
```

`platformio.ini` points `lib_deps` at `symlink://../crosspoint-simulator`, a
local clone, because this fork needs two shims the upstream simulator does not
carry (see **Fork shims** below). Those shims live in
`sim/simulator-shims.patch` so the setup is reproducible on another machine;
without the `git apply` step the build fails on
`readBatteryPercentage` and `mbedtls_sha256`.

The patch was taken against simulator commit `bc02574`. If it stops applying,
upstream has moved: re-create the two shims by hand from the table below and
refresh the patch with `git -C ../crosspoint-simulator diff > sim/simulator-shims.patch`.

## Running

```sh
sim/run.sh '0:WAIT:Entering activity: Home;800:SHOT:sim/out/home.bmp;1500:QUIT'
SIM_WINDOW=1 sim/run.sh '0:WAIT:Entering activity: Home;30000:QUIT'   # watch it live
```

Screenshots and the run log land in `sim/out/` (git-ignored). The SD card is
`fs_/` by default (`CROSSPOINT_SIM_SD`).

In a window, the keyboard stands in for the keys: arrows navigate, Return
confirms, Escape goes back, `P` is power, `S` enters the sleep screen, `H` is
home and `T` flips the slide switch.

## Script syntax, and the four traps

Steps are `<milliseconds>:<action>`, separated by `;`.

**Times are absolute, not deltas.** `800:UP;1600:ENTER;900:QUIT` quits at 900 ms
and the ENTER never happens. Times must increase across the whole script. After
a `WAIT` they restart from the moment it matched. This is the single easiest way
to waste an afternoon here: the run just ends early and nothing says why.

**A real SDL window needs a desktop session.** Scripted runs go headless with
`SDL_VIDEODRIVER=dummy`, which is what `sim/run.sh` does unless `SIM_WINDOW=1`.

**A run can outlive its script.** The simulator does not always act on `QUIT`,
and then the program keeps running until something kills it -- the firmware is
fine, the input script simply stops being delivered. Because `sim/run.sh` pipes
through `tee`, the log is complete long before that, so the symptom is a command
that never returns rather than missing output. Run anything interactive in the
background and kill it on a timer, then read `sim/out/last-run.log`.

**The firmware reopens the last book at boot.** `fs_/` is reused between runs,
so a run that left a book open does not reach Home, and the next script that
opens with `0:WAIT:Entering activity: Home` waits for a screen that never
arrives. `sim/run.sh` now exits non-zero when that happens instead of looking
like a clean run, but the fix is to quit from Home, or to clear
`fs_/.crosspoint/state.json`.

Actions: `UP`, `DOWN`, `LEFT`, `RIGHT`, `ENTER`, `BACK`, `POWER`, `SLEEP`,
`HOME`, `QUIT`, `SHOT:<path>`, `WAIT:<regex>` (matches a firmware log line;
no `;` inside). `<key>:<hold-ms>` sets the hold; the default is 80 ms.

`CROSSPOINT_SIM_SCREEN_TEXT=1` (on by default here) prints each screen's text as
one `[SIM] SCREEN:` log line, so a test can assert on a screen without reading
an image.

## Leaving the home screen

`LEFT` then `ENTER` reaches Settings from a fresh boot on every theme, so that
is what the example below uses. The reason differs by theme, and the difference
is worth knowing before writing any other home-screen script.

On the **cover-grid homes** (`uiTheme` Cover Grid or Goals Home) the four keys
are split into two bands rather than one list: `UP`/`DOWN` walk the covers, and
`LEFT`/`RIGHT` walk the row of destinations -- Files, Library, Statistics, the
conditional Plugins/OPDS slot, File Transfer, Settings. A `LEFT` or `RIGHT`
press while the selection is still on a cover jumps into the destination band
first, and `LEFT` enters it at the *last* entry, which is Settings.
`cycleBand()` in `src/activities/home/HomeActivity.cpp` is where that happens.

That slot is why `RIGHT` is the wrong way round to count: it enters the band at
the *first* entry, and whether Plugins or OPDS sits at position 3 depends on
`pluginsEnabled` and on whether a catalogue server is saved, so the number of
presses to any given destination is not fixed.

On the **default Lyra home** there is one flat ring instead, and `LEFT` is
`NavPrevious` (so is `UP`), which wraps from the first entry to the last --
again Settings.

So the trap is specific to the cover-grid homes: there, `UP` stays in the cover
band and `ENTER` opens a book, and the run then sits in the reader while the
rest of the script presses keys at it. On the Lyra home the same script reaches
Settings. `fs_/.crosspoint/settings.json` is reused between runs and carries
`uiTheme`, so which of these applies depends on what the last run left behind.

## Worked example

Home → Settings → System → Profiles, switching to the second profile:

```sh
sim/run.sh '0:WAIT:Entering activity: Home;800:LEFT;1600:ENTER;3000:ENTER;4000:DOWN;4400:DOWN;4800:DOWN;5200:DOWN;5600:DOWN;6000:DOWN;6800:ENTER;8000:SHOT:sim/out/profiles.bmp;8800:DOWN;9600:ENTER;11600:ENTER;13500:SHOT:sim/out/switched.bmp;14500:QUIT'
```

The six `DOWN` presses are the System list's row offset of Profiles, so a row
added above it moves the target. Assert on the screen rather than trusting the
count: `CROSSPOINT_SIM_SCREEN_TEXT` prints every row, so
`grep 'SCREEN:.*Profiles' sim/out/last-run.log` says whether the script
actually arrived.

## Fork shims

Two things the upstream simulator does not carry yet. Neither is specific to
this fork: both call sites are in committed upstream firmware.

| Shim | Why |
| --- | --- |
| `HalPowerManager::readBatteryPercentage` | `src/main.cpp:406` uses it to skip the sleep Wi-Fi join below 20%, and `lib/hal/HalPowerManager.h:61` declares it; the simulator had only `getBatteryPercentage()` |
| `mbedtls_sha256()` one-shot | `src/util/BookmarkUtil.cpp:44` calls it; the simulator's `mbedtls/sha256.h` had only the streaming API |

The simulator's `FORKING.md` asks for a GitHub fork rather than a patched local
clone -- "If you are re-applying the same patch by hand, you want a fork" -- and
it is right that editing `.pio/libdeps/simulator/` does not work, because
PlatformIO wipes that directory. A sibling clone escapes that, and a tracked
patch makes the setup reproducible from this repository, which is why it is done
this way here.

By `FORKING.md`'s own split, though, both shims belong upstream rather than in
any fork: the mbedTLS one-shot is a gap in the platform emulation layer ("a
missing enum value or an unstubbed function is a bug here"), and
`readBatteryPercentage` is a HAL method that is already in upstream firmware, so
the simulator is simply behind. Sending both upstream would retire this patch,
the sibling-clone requirement and the `git apply` step together, and let
`lib_deps` go back to a commit-pinned upstream URL.

The simulator's own `skip_device_content_protection.py` is vendored here as
`scripts/sim_skip_content_protection.py` rather than referenced across the
directory boundary. It is three lines of PlatformIO middleware, and it cannot be
loaded from `.pio/libdeps/` because a `symlink://` dependency resolves to a
`.pio-link` file, not a real directory. Keeping a copy in the repo means a
missing clone fails on a missing *library*, which PlatformIO explains, rather
than on a missing `extra_script`, which it does not.

## What it does not tell you

Upstream is explicit that the simulator "does not attempt to model controller
timing, LUT waveforms, ghosting, or power sequencing", and that it does not
simulate e-ink image quality, refresh behaviour, or memory pressure.

So it verifies layout, copy and navigation. Contrast, dithering on the real
panel, refresh artifacts and ghosting are only visible on the device, and
hardware testing is still required before a PR.

The SHA-256 shim is a placeholder, not a real digest. The simulator's streaming
implementation folds the input with `digest[i % 32] ^= input[i]`, and
`BookmarkUtil::syncId` keeps only the first 8 bytes, so two xpaths of the same
length that differ at a byte whose `offset % 32` is 8 or more produce the *same*
sync id -- which is the normal case for xpaths in one book. So bookmark sync
collides here and cannot be tested in the simulator, and nothing under
`fs_/.crosspoint/bookmarks/` should be copied onto a real card. Progress sync is
unaffected and can be exercised: it identifies a book by a partial MD5
(`KOReaderDocumentId::calculate`), not by this digest.
