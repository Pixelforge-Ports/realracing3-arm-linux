# Real Racing 3 — PortMaster port (experimental)

Runs the original Android `libRealRacing3.so` (2.7.0, armeabi-v7a) on an armhf
Linux handheld through a bionic ELF loader and a faked Android/JNI layer. No
emulator, no recompilation of the game.

**Status: playable, and slow.** It races on the hardware: menus, the driving
tutorial, sound and the controller all work. The frame rate is the honest
caveat — around 15-20 fps on an RK3326 handheld, because the game spends about
50 ms a frame in its own simulation on four Cortex-A35s. It is a 2014 phone
game asking for more CPU than this class of device has, and that part is not a
bug anyone can patch out.

## Bring your own game — read this part before anything else

The port ships no game data, and Real Racing 3 is harder to supply than most
ports on this device. Two things have to come across:

| | Where it comes from | Size |
|---|---|---|
| `libRealRacing3.so`, `libfmodex.so` | the **2.7.0** APK, `armeabi-v7a` | 12 MB |
| everything else | the app's **data folder** on the device | ~2.6 GB |

**The APK alone is not enough, and you cannot download the rest any more.**
The tracks, cars, audio and UI are content the game fetched at first run from
`0037-connect.cloudcell.com`, and that host no longer resolves. Installing the
APK on a phone today will not rebuild the data folder. So the donor has to be a
**complete backup of the installed game** — the app's data directory as it was
on a device that finished downloading, plus the APK (or the same `lib/` folder
inside it).

The exact library this port is built for:

    libRealRacing3.so   11,150,536 bytes
    sha1                615c9aa4a92faaf9a0f34750e344e5e8a6b9aedf
    sha256              dcad5752d22ea3df0070f95930aa6edee1a2662db0363ad5338deb8f94540ab9

Other 2.7.0 builds of the same library are accepted — the loader hooks by
exported symbol, not by fixed offset — but a different *version* is not.

The card must be exFAT or ext4, and needs ~2.6 GB free.

The game's fonts have to come across with it: on Android the text was drawn by
Skia, so this port rasterises every glyph itself from `EurostileLTStd*.otf`,
`myriadp0.otf`, `HelveticaNeueBold.ttf`, `LCD.ttf`, `minion_pro_italic.otf`,
`r3_symbols.ttf` and `r3_decals.ttf`, which sit at the top level of the data
directory. No typeface is bundled — without them the UI renders but stays
blank.

## Install

1. Copy `realracing3-portmaster.zip` into PortMaster's `autoinstall` folder and
   let PortMaster install it. Wait for "Finished running autoinstall", let it
   close on its own, then reboot through the firmware menu.
2. Put your game data in `ports/realracing3/` — a folder, a ZIP or an APK, in
   any combination. The filenames do not matter: the first launch identifies
   each donor by its contents.
3. Launch **Real Racing 3** from the Ports menu. The first start imports the
   data into `ports/realracing3/data/`, which takes a few minutes and needs the
   ~2.6 GB free. Do not power off during it. Later launches start normally.

An install made by hand before this port had an importer — the tree sitting
flat in `ports/realracing3/` — keeps working as it is and is not touched.

If something goes wrong the whole run is logged to
`ports/realracing3/log.txt` — that file is the first thing to look at.

## Controls

The port does not invent a control scheme. Real Racing 3 already ships one for
game controllers: the `[Android Gamepad]` profile inside the game's own
`joystick_config.txt`, which the engine loads for every pad it sees. Everything
below is that profile, with the handheld's buttons translated to the ordinals
the game's `ControllerManager` expects.

On a first launch the port also selects **Wheel B** (`Wheel_Manual`) as the
control scheme, because it is the only scheme in the game's own Controls menu
that both steers from the analog stick and leaves the throttle to the player.
The stock Android default, *Tilt A*, hands the throttle to the game and expects
an accelerometer this handheld does not have. Change it at any time from
**Settings → Controls** in the game; the port never overwrites your choice
after the first boot.

### Driving

| Control | Action |
|---|---|
| Left stick (left/right) | Steer |
| R2 | Throttle (analog) |
| L2 | Brake (analog) |
| R1 | Throttle (digital, full) |
| L1 | Brake (digital, full) |
| A | Throttle (digital, full) |
| Y | Brake (digital, full) |
| B | Look behind |
| X | Change camera |
| D-pad left / right | Steer (digital, full lock) |
| Start or Select | Pause |

Face buttons are the ones printed on the handheld: **A** is the right button,
**B** the bottom one, **X** the top one, **Y** the left one. The tutorial's
prompt *"press the topmost face button to change the camera"* therefore means
**X**, and it is correct.

Throttle and brake are analog on the triggers and all-or-nothing everywhere
else, which is how the game's own profile defines them — the shoulder and face
bindings exist as a fallback for pads without analog triggers. Do not hold a
trigger and its digital twin at once: the engine sums every binding of a slot,
so it just saturates.

### Menus

Some of the game's screens are driven by the controller and some were only ever
built for a touchscreen, so the port carries a software pointer for the second
kind. It starts visible.

| Control | Action |
|---|---|
| D-pad | Move the pointer |
| A | Tap |
| B | Back |
| Right stick | Scroll lists |
| L3 / R3 | Hide or show the pointer |
| Start | Pause, and bring the pointer back |

Moving either analog stick hides the pointer automatically — that is the port
switching to driving mode, and it is also what frees the d-pad to steer and the
A button to be a button again. Press **Start** or click a stick to get the
pointer back.

On the screens the game does drive with a controller, the d-pad moves the
highlight and A confirms — but only once the pointer is out of the way, since
the pointer gets first claim on both. Hide it with L3 if a menu ignores the
d-pad.

Two of these controls have been exercised end to end so far — steering on the
left stick and the throttle on R2, both through the driving tutorial. Every
other row is the binding the game's own profile declares and the port emits,
but nobody has confirmed it on screen yet, on this harness or on hardware.

## What is in the zip

    Real Racing 3.sh          launcher
    realracing3/realracing3   the loader (armhf)
    realracing3/libs.armhf/   the shared libraries the CFW does not provide,
                              with MANIFEST.txt and licenses/
    realracing3/realracing3.gptk
                              gptokeyb config, everything unbound on purpose:
                              input goes straight through the game's own JNI
                              exports, gptokeyb only serves the PortMaster
                              exit combination
    realracing3/port.json, gameinfo.xml, README.md, CREDITS.md

## Credits

Port and loader by EapRules. Real Racing 3 is © Electronic Arts / Firemint
(Firemonkeys). This project is not affiliated with either and redistributes
none of their files.
