#!/bin/bash
# PORTMASTER: realracing3-portmaster.zip, Real Racing 3.sh
#
# Real Racing 3 (Android 2.7.0, armeabi-v7a) — PortMaster launcher.
# Port and project by EapRules: https://github.com/EapRules
#
# The port never ships Firemint/EA's files. The user's extracted game tree
# lives next to the loader and must contain at least:
#
#   lib/armeabi-v7a/libRealRacing3.so
#   asset_list_base.txt
#   assets_480x320/
#
# This is a Java-driven JNI game, not a NativeActivity: the loader maps the
# Android .so with its own bionic ELF loader and answers the JNI calls itself.

# shellcheck disable=SC1090,SC1091,SC2154

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"

export PORT_32BIT="Y"

# Aspect-correct scaling for panels that are not 640x480 (the RG34XXSP's
# 720x480, for one). The game's output is a fixed 640x480 and the loader maps it
# onto whatever the panel really is: fit keeps 4:3 and letterboxes, stretch
# fills the panel, integer scales by a whole multiple and centres the result.
# The size comes from the GL drawable, so on a real 640x480 panel this is
# identity and costs nothing. On a firmware that reports the wrong size, set
# REALRACING3_PANEL_W / REALRACING3_PANEL_H here to force it.
export REALRACING3_SCALE="${REALRACING3_SCALE:-fit}"
[ -f "$controlfolder/tasksetter" ]          && source "$controlfolder/tasksetter"
[ -f "$controlfolder/device_info.txt" ]     && source "$controlfolder/device_info.txt"
[ -f "$controlfolder/mod_${CFW_NAME}.txt" ] && source "$controlfolder/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR="/$directory/ports/realracing3"
cd "$GAMEDIR" || exit 1

# Put the cover where the frontend looks for it.
#
# PortMaster is supposed to merge our gameinfo.xml into ports/gamelist.xml when
# it installs, and on some versions it does - two of this author's earlier ports
# got their artwork that way. It did not happen here, and since it is the
# frontend's own convention that every other title on the card relies on
# (ports/images/<the launcher's name>.png), the port can simply satisfy it
# itself instead of depending on which PortMaster the user happens to run.
#
# Copy only, once, and never overwrite: a user who put their own artwork there
# chose it on purpose.
_rr3_img_dir="/$directory/ports/images"
if [ -f "$GAMEDIR/cover.png" ] && [ ! -e "$_rr3_img_dir/Real Racing 3.png" ]; then
  mkdir -p "$_rr3_img_dir" 2>/dev/null
  cp "$GAMEDIR/cover.png" "$_rr3_img_dir/Real Racing 3.png" 2>/dev/null \
    && echo "Artwork installed to ports/images/Real Racing 3.png"
fi

# ...and point the frontend's own index at it.
#
# Dropping the file in images/ is only half of it: EmulationStation reads
# ports/gamelist.xml, and PortMaster is what normally writes our <image> there
# from gameinfo.xml at install time. That merge is skipped, silently and
# without a log line, whenever harbourmaster does not recognise the OS name -
# any fork or re-release lands on PlatformBase, whose gamelist_file() returns
# None, and gamelist_backup() then yields None and returns. Nothing fails, no
# port is broken, the artwork simply never arrives. Rather than depend on which
# firmware the user runs, satisfy the convention ourselves.
#
# Deliberately conservative: never touch a gamelist that does not exist (muOS,
# TrimUI and RetroDECK do not use one), never overwrite an <image> the user
# already has, back up before writing, and only install the result if it still
# parses as the same document plus our line.
_rr3_gamelist="/$directory/ports/gamelist.xml"
if [ -e "$_rr3_img_dir/Real Racing 3.png" ] && [ -s "$_rr3_gamelist" ]; then
  _rr3_tmp="$GAMEDIR/.gamelist.$$"
  if awk -v P="./Real Racing 3.sh" -v IMG="./images/Real Racing 3.png" \
         -v NAME="Real Racing 3" '
      { L[++n] = $0 }
      END {
        s = 0; found = 0; hasimg = 0; ins = 0; pad = "\t\t"
        for (i = 1; i <= n; i++) {
          if (L[i] ~ /<game>/) { s = i }
          if (L[i] ~ /<\/game>/ && s > 0) {
            hit = 0; img = 0; pl = 0
            for (j = s; j <= i; j++) {
              if (index(L[j], "<path>" P "</path>") > 0) { hit = 1; pl = j }
              if (L[j] ~ /<image>/) { img = 1 }
            }
            if (hit == 1) { found = 1; hasimg = img; ins = pl }
            s = 0
          }
        }
        if (found == 1 && hasimg == 1) { exit 1 }
        if (found == 1) {
          match(L[ins], /^[ \t]*/)
          pad = substr(L[ins], 1, RLENGTH)
          for (i = 1; i <= n; i++) {
            print L[i]
            if (i == ins) { print pad "<image>" IMG "</image>" }
          }
          exit 0
        }
        done = 0
        for (i = 1; i <= n; i++) {
          if (L[i] ~ /<\/gameList>/ && done == 0) {
            print "\t<game>"
            print "\t\t<path>" P "</path>"
            print "\t\t<name>" NAME "</name>"
            print "\t\t<image>" IMG "</image>"
            print "\t</game>"
            done = 1
          }
          print L[i]
        }
        if (done == 0) { exit 1 }
        exit 0
      }' "$_rr3_gamelist" > "$_rr3_tmp" 2>/dev/null; then
    # Only swap it in if the result is a sane, complete document.
    if [ -s "$_rr3_tmp" ] \
       && grep -q "</gameList>" "$_rr3_tmp" \
       && grep -q "images/Real Racing 3.png" "$_rr3_tmp"; then
      cp "$_rr3_gamelist" "$_rr3_gamelist.bak" 2>/dev/null
      if cp "$_rr3_tmp" "$_rr3_gamelist" 2>/dev/null; then
        echo "Artwork registered in ports/gamelist.xml"
      fi
    fi
  fi
  rm -f "$_rr3_tmp" 2>/dev/null
fi
unset _rr3_img_dir _rr3_gamelist _rr3_tmp

: > "$GAMEDIR/log.txt"
exec > "$GAMEDIR/log.txt" 2>&1

# CFWs do not ship libzip/libbsd/libmd/libcrypto and their libstdc++ may
# predate the toolchain's; the port bundles the exact set it was linked
# against (tools/collect_libs.sh, see libs.armhf/MANIFEST.txt).
export LD_LIBRARY_PATH="$GAMEDIR/libs.armhf${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-}"
export LOADER_TRACE=1

# Audio routing is decided by what the device actually runs, never by CFW
# name. src/main.cpp calls SDL_Init with SDL_INIT_AUDIO and treats a failure as
# fatal, so an unroutable PCM would kill the game before the first frame. If a
# user audio server is present (PipeWire, or a PulseAudio socket), the 32-bit
# game must route through it or it grabs a PCM nobody is listening to. If none
# is found, fall back to ALSA dmix, which is what a bare-ALSA CFW (the R36S on
# ArkOS) provides.
_RR3_PW=""
for _pw in /usr/lib32/pipewire-0.3 /usr/lib/arm-linux-gnueabihf/pipewire-0.3; do
  [ -d "$_pw" ] && { _RR3_PW="$_pw"; break; }
done
for _xrd in "${XDG_RUNTIME_DIR:-}" /run/user/0 /var/run/user/0; do
  [ -n "$_xrd" ] && [ -d "$_xrd" ] && { export XDG_RUNTIME_DIR="$_xrd"; break; }
done
_RR3_PULSE=""
for _pulse in "${XDG_RUNTIME_DIR:-}/pulse/native" /run/pulse/native /var/run/pulse/native; do
  [ -n "$_pulse" ] && [ -S "$_pulse" ] && { _RR3_PULSE="$_pulse"; break; }
done
if [ -n "$_RR3_PW" ] || [ -n "$_RR3_PULSE" ]; then
  unset AUDIODEV ALSA_CONFIG_PATH SDL_AUDIO_DEVICE_NAME ALSA_CARD
  export SDL_AUDIODRIVER=alsa
  export ALSOFT_DRIVERS=alsa
  export SDL_AUDIO_ALSA_SET_BUFFER_SIZE=1
  for _spa in /usr/lib32/spa-0.2 /usr/lib/arm-linux-gnueabihf/spa-0.2; do
    [ -d "$_spa" ] && { export SPA_PLUGIN_DIR="$_spa"; break; }
  done
  [ -n "$_RR3_PW" ] && export PIPEWIRE_MODULE_DIR="$_RR3_PW"
  if [ -n "$_RR3_PULSE" ]; then
    export PULSE_SERVER="unix:$_RR3_PULSE"
  else
    unset PULSE_SERVER
  fi
  echo "Audio: routing through the device's audio server (PipeWire/Pulse), dmix bypassed"
else
  export AUDIODEV="${AUDIODEV:-plug:dmix}"
  export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
  echo "Audio: ALSA dmix (no audio server detected)"
fi

# Which of FMOD's two Android outputs to use. This is an either/or, not a
# fallback: FMOD picks one by asking dlopen for libOpenSLES.so and only
# initialises the one it picked. The default is OpenSL ES, served natively by
# android/opensles.cpp. Setting this makes the loader refuse OpenSL on purpose,
# which sends FMOD to its AudioTrack output and hands the mixing to
# src/rr3_fmod_pump.cpp - the thread that runs in Java on a phone. Left here
# because the two can only be compared on real hardware.
#
#   export REALRACING3_FMOD_OUTPUT=audiotrack

CUR_TTY=/dev/tty0
[ -w "$CUR_TTY" ] || CUR_TTY=/dev/tty1

show_screen() {
  $ESUDO chmod 666 "$CUR_TTY" 2>/dev/null
  printf "\033c" > "$CUR_TTY"
  cat > "$CUR_TTY"
  sleep "${1:-10}"
  printf "\033c" > "$CUR_TTY"
}

# Bring your own game.
#
# Where the tree lives depends on how it got here. An install made by hand
# before this port had an importer put everything flat in the port folder, and
# those keep working untouched. eapx stages into data/ instead, because it
# refuses to commit onto the port's own directory - and rightly so: this game's
# tree is 240 entries at its root and would sit interleaved with the launcher,
# the loader and libs.armhf.
if [ -f "$GAMEDIR/asset_list_base.txt" ]; then
  RR3_DATA_DIR="$GAMEDIR"          # hand-made flat install, from before eapx
else
  RR3_DATA_DIR="$GAMEDIR/data"
fi
GAME_SO="$RR3_DATA_DIR/lib/armeabi-v7a/libRealRacing3.so"

# A release user should not have to unpack an Android backup by hand. eapx
# recognises a donor by its contents - folder, ZIP or APK, any filename - stages
# the tree away from the live install, validates the exact native library and
# only publishes data/ once it is complete.
if [ ! -f "$GAME_SO" ] || [ ! -f "$RR3_DATA_DIR/asset_list_base.txt" ] \
   || [ ! -d "$RR3_DATA_DIR/assets_480x320" ]; then
  if ! command -v python3 >/dev/null 2>&1; then
    echo "Game-data import failed: python3 is unavailable"
    show_screen 12 <<EOF

  Real Racing 3 - Python 3 missing

  Automatic game-data import needs
  Python 3 from the CFW.

  Update PortMaster/your firmware, or
  extract the donor on a computer into:
    ports/realracing3/data/

EOF
    pm_finish
    exit 1
  fi

  if [ ! -f "$GAMEDIR/eapx.py" ] || [ ! -f "$GAMEDIR/realracing3.eapx.json" ]; then
    echo "Game-data import failed: eapx runtime or recipe is missing"
    show_screen 12 <<EOF

  Real Racing 3 - incomplete port

  eapx.py or realracing3.eapx.json
  is missing. Reinstall the release ZIP
  through PortMaster/autoinstall.

EOF
    pm_finish
    exit 1
  fi

  echo "Game data is absent; starting content-based first-boot import"
  if ! python3 "$GAMEDIR/eapx.py" install \
       --recipe "$GAMEDIR/realracing3.eapx.json" \
       --game-dir "$GAMEDIR" --tty "$CUR_TTY"; then
    echo "Game-data import failed; see $GAMEDIR/eapx.log"
    show_screen 20 <<EOF

  Real Racing 3 - game data not ready

  Put your own Real Racing 3 2.7.0
  Android install in:
    ports/realracing3/

  A folder, ZIP or APK - the filename
  does not matter.

  The APK alone is NOT enough: the
  ~2.6 GB of tracks, cars and audio
  are downloaded content, and EA's
  server for them is gone. You need a
  backup of the app's data folder.

  See README.md and eapx.log.

EOF
    pm_finish
    exit 1
  fi
  RR3_DATA_DIR="$GAMEDIR/data"
  GAME_SO="$RR3_DATA_DIR/lib/armeabi-v7a/libRealRacing3.so"
fi

rm -f "$GAMEDIR/PUT_REAL_RACING_3_DATA_HERE.txt"

# The loader hooks the game by exported symbol, not by fixed offset, so a
# regional or resigned 2.7.0 copy is fine. A size sanity-check still catches a
# manually dropped library from a different build, and only warns.
EXPECTED_SIZE=11150536
GAME_SIZE=$(stat -c%s "$GAME_SO" 2>/dev/null || stat -f%z "$GAME_SO" 2>/dev/null)
if [ -n "$GAME_SIZE" ] && [ "$GAME_SIZE" != "$EXPECTED_SIZE" ]; then
  echo "Warning: libRealRacing3.so size=$GAME_SIZE expected=$EXPECTED_SIZE (2.7.0); continuing"
fi

# SDL must create its context through the 32-bit Mali blob, and the loader's
# GLES1 table (thunks/khronos/gles1.cpp) dlopen()s "libmali.so.1" by name. On
# the console the blob is installed as libmali-bifrost-g31-rxp0-gbm.so and no
# libmali.so.1 exists anywhere on the linker path, so without this shim that
# table stays empty and src/symtab_glprobe.cpp silently drops every glClear,
# glDrawArrays, glDrawElements and glTexImage2D it wraps: a black screen with
# no error at all. Build the symlinks in /tmp because the SD card may be exFAT
# and cannot hold symlinks. Try the exact R36S filename first, then any Mali
# build in the standard 32-bit library directories, so other Mali handhelds
# work without naming their SoC here.
MALI_BLOB=""
for candidate in \
  /usr/lib/arm-linux-gnueabihf/libmali-bifrost-g31-rxp0-gbm.so \
  /usr/lib/arm-linux-gnueabihf/libMali.so \
  /usr/lib/arm-linux-gnueabihf/libmali.so.1; do
  [ -e "$candidate" ] && { MALI_BLOB="$candidate"; break; }
done
if [ -z "$MALI_BLOB" ]; then
  for _gldir in \
    /usr/lib/arm-linux-gnueabihf \
    /usr/lib/arm-linux-gnueabihf/mali \
    /usr/lib32 \
    /lib/arm-linux-gnueabihf; do
    [ -d "$_gldir" ] || continue
    for _cand in "$_gldir"/libmali*.so* "$_gldir"/libMali.so*; do
      [ -e "$_cand" ] && { MALI_BLOB="$_cand"; break; }
    done
    [ -n "$MALI_BLOB" ] && break
  done
fi

if [ -n "$MALI_BLOB" ]; then
  GL_SHIM="/tmp/realracing3-gl"
  rm -rf "$GL_SHIM"
  if mkdir -p "$GL_SHIM" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libEGL.so.1" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv1_CM.so.1" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv2.so.2" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libmali.so.1"; then
    export LD_LIBRARY_PATH="$GL_SHIM:$LD_LIBRARY_PATH"
    echo "GL: using Mali blob $MALI_BLOB"
  else
    echo "GL: failed to create /tmp shim, using system libraries"
  fi
else
  echo "GL: no compatible 32-bit Mali blob found"
fi

# A zip extracted onto exFAT/FAT32 loses the executable bit; without this the
# launcher would die with "Permission denied" and never reach the loader.
$ESUDO chmod +x "$GAMEDIR/realracing3"

# Diagnostics for the first hardware runs. This port has never executed on a
# real device, and the emulator is known to misreport rendering: there
# libGL.so.1 (desktop GL) and SDL's GLES2 are two different drivers, while on
# this hardware libmali exports EGL, GLESv1_CM and GLESv2 from a single blob.
# So the numbers that matter can only be taken here.
#
# GL_STATS is a periodic census (draw calls, vertices, textures by format,
# framebuffer completeness, and a dispatch audit naming which library resolved
# each entry point). It costs one line every few hundred frames.
# REALRACING3_GL_STATS_ERRORS is deliberately NOT enabled: it calls glGetError after every draw,
# which would distort the frame rate this run is meant to measure.
# Set REALRACING3_QUIET=1 to turn all of it off once the port is trusted.
if [ -z "${REALRACING3_QUIET:-}" ]; then
  export REALRACING3_GL_STATS=1
  export REALRACING3_GL_STATS_EVERY=300
  export LOADER_TRACE=1
else
  unset LOADER_TRACE
fi

# Escape hatch for the first run, not enabled by default. Under Mesa the game's
# full-screen composite pass paints over the finished scene, because the
# emulator lacks GL_EXT_shader_framebuffer_fetch and the port rewrites that
# shader onto a sampler2D branch that nothing fills. This GPU supports the
# extension natively, so the rewrite should not happen here and the pass should
# work - but if the screen comes back blank on hardware anyway, re-running with
# REALRACING3_SKIP_COMPOSITE_QUAD=1 says whether that pass is the culprit.
[ -n "${REALRACING3_SKIP_COMPOSITE_QUAD:-}" ] && \
  echo "NOTE: composite quad skipped by request (diagnostic mode)"

echo "=== Real Racing 3 port: run started $(date '+%Y-%m-%d %H:%M:%S') ==="
echo "device: ${CFW_NAME:-unknown} / ${DEVICE_NAME:-unknown}  resolution: ${DISPLAY_WIDTH:-?}x${DISPLAY_HEIGHT:-?}"
echo "memory: $(awk '/MemTotal|MemAvailable/ {printf "%s=%dMB ", $1, $2/1024}' /proc/meminfo 2>/dev/null)"
echo "loader: $(stat -c%s "$GAMEDIR/realracing3" 2>/dev/null) bytes"

# Controls are delivered directly through the game's own JNI exports
# (android/input_bridge.cpp: onTouch*/onKey*/ControllerManager). gptokeyb runs
# with everything unbound, only so PortMaster's standard exit combination can
# terminate the port.
$GPTOKEYB "realracing3" -c "$GAMEDIR/realracing3.gptk" &

if command -v pm_platform_helper >/dev/null 2>&1; then
  pm_platform_helper "$GAMEDIR/realracing3"
fi

RUN_START=$(date +%s)
# argv[1] is the game tree, which is not always the port folder any more - see
# the RR3_DATA_DIR note above. Everything in the loader's path handling is
# relative to this one argument.
$TASKSET "$GAMEDIR/realracing3" "$RR3_DATA_DIR"
GAME_RC=$?
RUN_SECONDS=$(( $(date +%s) - RUN_START ))

# A one-line verdict at the tail of the log, so the first thing read after a
# hardware run answers "did it run, for how long, and how did it end" without
# scrolling through the trace.
echo "=== run finished: exit=$GAME_RC after ${RUN_SECONDS}s ==="
echo "memory at exit: $(awk '/MemAvailable/ {printf "%dMB free", $2/1024}' /proc/meminfo 2>/dev/null)"
if [ "$GAME_RC" -ge 128 ]; then
  echo "NOTE: exit >= 128 means the process was killed by signal $((GAME_RC - 128))"
fi
grep -cE "\*\*\* DROPPED" "$GAMEDIR/log.txt" 2>/dev/null \
  | awk '{ if ($1 > 0) print "WARNING: " $1 " GL call(s) were dropped - see DROPPED lines above" }'

$ESUDO kill -9 "$(pidof gptokeyb)" 2>/dev/null
rm -rf /tmp/realracing3-gl
unset LD_LIBRARY_PATH SDL_GAMECONTROLLERCONFIG

pm_finish
exit "$GAME_RC"
