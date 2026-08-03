# Real Racing 3 — native ARM port

Runs the 2013 EA/Firemonkeys **Real Racing 3** mobile game on Linux/ARM
handhelds by loading its original Android native library into a bionic/JNI
compatibility layer. No emulator and no Android runtime is involved.

**Port and project by [EapRules](https://github.com/EapRules).**

This directory targets the **v2.7.0 ROW** build:

```text
lib/armeabi-v7a/libRealRacing3.so
SHA1 615c9aa4a92faaf9a0f34750e344e5e8a6b9aedf
```

Any legitimate 2.7.0 copy works. Validation checks the exact bytes the loader
patches — its critical regions — rather than one whole-file hash, so regional
variants and re-signed packages are accepted while a genuinely different build
is rejected before it can crash.

## Current status

Playable. It boots, runs the Android lifecycle, compiles the game's 116
shaders, renders the front-end and the 3D scene, drives on a gamepad and has
sound.

The frame rate is the honest caveat: roughly 15-20 fps on an RK3326-class
handheld. The frame is spent inside the game's own simulation — about 50 ms of
CPU per frame on four Cortex-A35 cores, against 128-250 draw calls and ~43k
triangles that the GPU handles comfortably. It is a 2014 phone game asking for
more CPU than that class of device has, and no amount of porting work removes
that. Faster ARM handhelds should do better; reports are welcome.

## Device-generic by construction

Every hardware-specific decision is made by capability, not by device name, so
this is not one handheld's port that happens to run elsewhere:

- **glibc** — built against an old glibc so it also runs on older firmwares.
  The build refuses to produce a binary or a bundled library that asks for
  more than the floor (`tools/check_glibc_floor.sh`).
- **Screen** — the game's output is a fixed 640×480. The loader reads the real
  panel size and maps onto it: `fit` (letterboxed, no distortion, the default),
  `stretch`, or `integer`. On a 640×480 panel this is identity and costs
  nothing.
- **Audio** — detects a running audio server (PipeWire/PulseAudio) and routes
  through it, otherwise falls back to ALSA dmix.
- **GPU** — finds the device's own Mali blob by pattern rather than by one
  SoC's filename, and builds the loader's GL shim around whatever it finds.

## Bring your own game

Nothing here ships game data, and this title needs more of it than most. Two
things have to come across:

| | Where it comes from | Size |
|---|---|---|
| `libRealRacing3.so`, `libfmodex.so` | the 2.7.0 APK, `armeabi-v7a` | 12 MB |
| everything else | the app's data directory | ~2.4 GB |

**The APK alone is not enough, and the rest can no longer be downloaded.** The
tracks, cars, audio and UI are content the game fetched at first run from a
host that no longer resolves, so the donor has to include a backup of the app's
data directory from a device that finished downloading. Get the Mali/ETC
texture family: the Adreno, PowerVR and Tegra sets are encodings this GPU class
cannot read.

The first launch imports the donor with [eapx](https://github.com/EapRules),
which recognises a folder, ZIP or APK by its contents in any combination and
publishes the tree only once it validates.

## Build

```bash
docker build -f Dockerfile.build -t realracing3-build .
docker run --rm -v "$PWD":/src -w /src realracing3-build make -j4
```

The bundled shared libraries are generated, never checked in:

```bash
docker run --rm -v "$PWD":/src -w /src realracing3-build make libs
```

`make libs` runs `tools/collect_libs.sh` (which writes `MANIFEST.txt` and
`licenses/`) and `tools/check_glibc_floor.sh`. Never assemble
`build/libs.armhf/` by hand — `package_portmaster.sh` refuses a directory
without those two.

## Package

```bash
./package_portmaster.sh     # -> build/realracing3-portmaster.zip
```

The script validates the PortMaster signature and the file list, verifies that
the packaged binary is the one just built, and refuses to continue if any
proprietary game file made it into the zip.

## PortMaster install

1. Put `realracing3-portmaster.zip` in `tools/PortMaster/autoinstall/`.
2. Put your own game data in `ports/realracing3/` — folder, ZIP or APK, any
   filename.
3. Open PortMaster, wait for `Finished running autoinstall`, let it close.
4. Reboot through the firmware menu.

The first launch imports the data and needs the free space for it; later
launches start normally.

## Controls

The port does not invent a scheme. The game already ships one for controllers —
the `[Android Gamepad]` profile in its own `joystick_config.txt` — and this is
that profile with the handheld's buttons translated to the ordinals the game's
`ControllerManager` expects.

| Control | Action |
|---|---|
| Left stick (left/right) | Steer |
| R2 / L2 | Throttle / brake (analog) |
| R1 / L1 | Throttle / brake (digital, full) |
| A / Y | Throttle / brake (digital, full) |
| B | Look behind |
| X | Change camera |
| D-pad left / right | Steer (digital, full lock) |
| Start or Select | Pause |

Throttle and brake are analog on the triggers and all-or-nothing everywhere
else, which is how the game's own profile defines them: the shoulder and face
bindings are the fallback for pads without analog triggers.

On a first launch the port selects **Wheel B** (`Wheel_Manual`) as the control
scheme, because it is the only one in the game's own Controls menu that steers
from the analog stick and still leaves the throttle to the player. The stock
Android default is *Tilt A*, which hands the throttle to the game and expects
an accelerometer a handheld does not have. Change it any time from **Settings →
Controls**; the port never overwrites that choice after the first boot.

## Runtime options

All optional; the defaults are what the port ships with.

| Variable | Default | What it does |
|---|---|---|
| `REALRACING3_SCALE` | `fit` | `fit`, `stretch` or `integer` panel mapping |
| `REALRACING3_PANEL_W/H` | auto | force the panel size if the firmware misreports it |
| `REALRACING3_VSYNC` | off | wait for vblank; costs a frame's latency |
| `REALRACING3_FMOD_OUTPUT` | OpenSL | `audiotrack` uses the other FMOD backend |
| `REALRACING3_ASSET_PACK` | `assets_480x320` | which resolution bucket to load |
| `REALRACING3_QUIET` | off | silence the trace log |

`REALRACING3_FMOD_OUTPUT` is the one worth knowing about on unfamiliar
hardware: the game's audio engine has two Android backends and only ever
initialises the one it picks, so if OpenSL cannot be opened on some device,
that switch drives the other one without rebuilding anything.

## Layout

```text
loader/       vendored bionic ELF loader (ARM32)
src/          loader entry point, symbol tables, per-game patches
thunks/       libc / libm / zlib / GLES / OpenSL ES tables
android/      EGL shim, asset manager, input bridge, logging
jni/          fake JVM and the registered fake Java classes
emulator/     qemu-arm harness and the screenshot/input driver
ports/        the PortMaster launcher and package metadata
tools/        library collection, glibc floor check, packaging helpers
```

## Diagnostics

The whole run is logged to `ports/realracing3/log.txt` — that file is the first
thing to look at. The lines that answer most questions:

```text
frame budget over N frames: render … swap … audio …   where the frame goes
opensl: depth …/… ms … draining at N% of real time    audio pacing
opensl: enqueued block=… nonzero=… peak=…             is there signal at all
asset patch: N entries examined, M skipped as absent  donor completeness
```
