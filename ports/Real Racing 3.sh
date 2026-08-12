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

# A zip extracted onto exFAT/FAT32 loses the executable bit. This used to sit
# just before the game was launched; it has to happen here instead, because the
# version query below and the GL preflight further down both run the binary.
$ESUDO chmod +x "$GAMEDIR/realracing3" 2>/dev/null

# Which build produced this log. A user reporting a problem is running whatever
# is on their SD card, not necessarily the release they just downloaded, and a
# log that does not name its build cannot be told apart from one produced by the
# release before it. The string lives in the binary (src/port_version.h) and is
# asked for here, so a launcher and a loader can never claim different versions.
#
# The bundled libraries are not on LD_LIBRARY_PATH yet (that export happens
# further down); without them the binary cannot link and the answer comes back
# empty - on the sibling port a real device printed "vunknown" for exactly this.
# The path rides along just for this one call.
PORT_VERSION=$(LD_LIBRARY_PATH="$GAMEDIR/libs.armhf${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$GAMEDIR/realracing3" --version 2>/dev/null) || PORT_VERSION=""
echo "Real Racing 3 port v${PORT_VERSION:-unknown} launcher starting"

# The machine, in every log, whether or not anything goes wrong.
#
# Each line below was asked for by hand in a bug report at least once. Asking
# costs days of round trips with a user who is on a different continent and a
# different firmware, and the answers do not change between runs - so they are
# collected unconditionally. The whole block is a dozen lines and prefixed
# "sys:" so it greps out of the log cleanly.
#
# GL_DIRS is defined here rather than beside the provider search below because
# the survey lists them; the search is what explains them.
# /usr/local/lib first: on the ArkOS builds that carry their working 32-bit
# GL set there (reported by R36S users; credit to Bheathy on Reddit for
# finding the path), the sets under /usr/lib/arm-linux-gnueabihf exist but do
# not load, so the search order is what makes the difference.
GL_DIRS="/usr/local/lib/arm-linux-gnueabihf /usr/lib/arm-linux-gnueabihf \
/usr/lib/arm-linux-gnueabihf/mali \
/lib/arm-linux-gnueabihf /usr/lib32/mali /usr/lib32"
if [ "$DEVICE_ARCH" = "armhf" ]; then
  GL_DIRS="$GL_DIRS /usr/lib /lib"
fi

echo "sys: uname: $(uname -rm 2>/dev/null)"
_sys_os=$(sed -n 's/^PRETTY_NAME="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' /etc/os-release 2>/dev/null | head -n 1)
[ -n "$_sys_os" ] || _sys_os=$(cat /etc/*-release 2>/dev/null | head -n 1)
echo "sys: os: ${_sys_os:-unknown}"
echo "sys: cfw: ${CFW_NAME:-unknown} device: ${DEVICE_NAME:-unknown} arch: ${DEVICE_ARCH:-unknown}"
# What GL the firmware actually ships, seen rather than asked about. Filtered to
# the sonames that decide whether this port can run: an unfiltered listing of a
# multiarch library directory is hundreds of names and would bury the block it
# belongs to.
for _sys_gldir in $GL_DIRS; do
  [ -d "$_sys_gldir" ] || continue
  _sys_gl=$(ls "$_sys_gldir" 2>/dev/null \
      | grep -E '^lib(EGL|GLESv1_CM|GLESv2|mali|Mali|GLdispatch|gbm\.|drm\.)' \
      | tr '\n' ' ')
  echo "sys: gl $_sys_gldir: ${_sys_gl:-(no GL libraries)}"
done
# Permissions included on purpose: a render node the user cannot open fails the
# same way a missing driver does.
_sys_dri=$(ls -la /dev/dri 2>/dev/null | sed 1d \
    | awk 'NF>=9 {print $NF" ("$1" "$3":"$4")"}' | tr '\n' ' ')
echo "sys: dri: ${_sys_dri:-none}"
_sys_mem=$(free -m 2>/dev/null | sed -n '2p' | tr -s ' ')
[ -n "$_sys_mem" ] || _sys_mem=$(grep -E '^Mem(Total|Available)' /proc/meminfo 2>/dev/null | tr -s ' \n' ' ')
echo "sys: mem: ${_sys_mem:-unknown}"
_sys_sdl=$(ls "$GAMEDIR"/libs.armhf/libSDL2*.so* 2>/dev/null | xargs -n1 basename 2>/dev/null | tr '\n' ' ')
echo "sys: sdl bundled: ${_sys_sdl:-none}"

# CFWs do not ship libzip/libbsd/libmd/libcrypto and their libstdc++ may
# predate the toolchain's; the port bundles the exact set it was linked
# against (tools/collect_libs.sh, see libs.armhf/MANIFEST.txt).
export LD_LIBRARY_PATH="$GAMEDIR/libs.armhf${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_GAMECONTROLLERCONFIG="${sdl_controllerconfig:-}"
export LOADER_TRACE=1

# muOS curates its SDL controller mappings so the logical buttons already
# match the printed labels; the Nintendo-style swap that is right on the
# ArkOS family double-swaps there (RG 40XX-H report - the "ABXY feels like
# Xbox layout" symptom). The default follows the CFW; the variable still
# overrides either way.
case "$(echo "${CFW_NAME:-}" | tr 'A-Z' 'a-z')" in
  muos) _face_default=xbox ;;
  *)    _face_default=nintendo ;;
esac
export REALRACING3_FACE_LAYOUT="${REALRACING3_FACE_LAYOUT:-$_face_default}"

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

# SDL must create its context through the device's own 32-bit GL stack, and the
# loader's GLES1 table (thunks/khronos/gles1.cpp) dlopen()s "libmali.so.1" by
# name. On the console the blob is installed as libmali-bifrost-g31-rxp0-gbm.so
# and no libmali.so.1 exists anywhere on the linker path, so without a shim that
# table stays empty and src/symtab_glprobe.cpp silently drops every glClear,
# glDrawArrays, glDrawElements and glTexImage2D it wraps. Build the symlinks in
# /tmp because the SD card may be exFAT and cannot hold symlinks.
#
# Which stack that is depends on the device, not on the firmware's name, so it
# is found by capability:
#
#   1. A unified Mali blob under one of the exact tested filenames - one .so
#      exporting EGL, GLESv1_CM and GLESv2. Known-good and therefore first.
#   2. A split Mali wrapper set - a directory holding both libEGL.so and
#      libGLESv2.so, the layout a Batocera-derived firmware installs. SDL is
#      pointed straight at those two files (SDL_VIDEO_EGL_DRIVER /
#      SDL_VIDEO_GL_DRIVER) rather than being left to find a blob.
#   3. Any other Mali blob in the 32-bit library directories, because every
#      distribution names it differently: versioned upstream names on
#      Debian-style CFWs (libmali-bifrost-g31-*.so), an unversioned libmali.so.1
#      on Buildroot ones, libMali.so where a firmware symlinks it.
#   4. No Mali anything, but a real 32-bit EGL/GLES set - a Mesa/glvnd userland,
#      which is what a Panfrost-only device ships and what the build container
#      has, so it is the tier the harness exercises on every run.
#   5. None of those. Say so on screen instead of leaving the user with a black
#      panel: without a 32-bit provider SDL either falls back to something that
#      never reaches the framebuffer, or fails to create a window at all.
#
# Why the wrapper set sits between the two blob tiers, and not elsewhere:
#
#   - It must come after tier 1 so that every device already working keeps
#     working unchanged. A Debian-style CFW that ships the tested blob usually
#     also ships unversioned libEGL.so/libGLESv2.so symlinks beside it; if the
#     wrapper tier ran first it would win there and change a happy path for no
#     reason. Tier 1 matches three literal filenames, so it is cheap to keep in
#     front.
#   - It must come before tier 3, and that is the whole fix. A Knulli device has
#     /usr/lib32/libmali.so.0 next to the wrapper set: the old glob picks the
#     blob, the preflight can even pass on it, and SDL still dies in
#     SDL_CreateWindow. The wrapper set is the stack that firmware actually
#     supports, so it has to be asked for first. A Knulli Gladiator user got
#     this port running by hand-editing exactly those two variables plus
#     SDL_VIDEODRIVER=mali into the launcher.
#
# The discriminator for tier 2 is the *unversioned* pair libEGL.so +
# libGLESv2.so, present together in one directory. A runtime Mesa/glvnd rootfs
# ships only the versioned sonames (libEGL.so.1, libGLESv2.so.2); the
# unversioned names are how the split Mali wrapper installs itself. Matching on
# them keeps tier 4 for Mesa, where it belongs.
#
# The directories searched are GL_DIRS, set with the system survey at the top of
# this script. They are architecture-scoped, so a 64-bit library can never be
# picked: the multiarch triplet dir and lib32 are 32-bit by definition, and the
# bare /usr/lib and /lib are only consulted on a pure-armhf rootfs.
#
# A candidate that exists is not a driver that works. On a 64-bit userland the
# 32-bit directories can hold an orphaned blob whose own dependencies were never
# installed: /usr/lib32/libmali.so.0 was picked on a muOS device running the
# sibling port, SDL answered "Can't load EGL/GL library on window creation", and
# every GL import resolved to nil. Existence was checked; loadability was not.
#
# So every candidate is dlopen()ed before it is committed to. The probe is the
# port's own binary (--gl-probe): it is 32-bit, it is already here, and it loads
# the library the same way SDL will, in the same runtime linker and the same
# LD_LIBRARY_PATH. ldd would have been simpler and would have been wrong on
# exactly the devices this is for - it execs the host's interpreter list, so on
# a 64-bit rootfs it reports an armhf .so as "not a dynamic executable".
#
# A probe that cannot run at all is not a verdict: the candidate is accepted
# unchecked, which is the behaviour before this check existed.
#
# Acceptance is logged as well as rejection. Silence on the happy path would
# make a passing preflight indistinguishable from a release without one.
GL_PROBE_REASON=""
GL_REJECTED=""
GL_FIRST_REASON=""
#
# The symbol the candidate must resolve is a parameter because the tiers below
# ask three different questions of three different kinds of library: does this
# provide EGL (eglGetDisplay), does it provide GLES 2 (glGetString), does it
# provide fixed function (glMatrixMode - not because this game calls fixed
# function, it calls none, but because that is the symbol gl_provider_open() in
# thunks/khronos/gles1.cpp tests before adopting a library for the GLES1 table).
# A library rejected for one symbol may be the right answer for another, so the
# rejection cache is keyed by both.
# Mesa's driver is loaded by glvnd, and this port can hide it from itself.
#
# glvnd dlopens the driver named in /usr/share/glvnd/egl_vendor.d/*.json by
# bare soname, so that dlopen walks LD_LIBRARY_PATH - where this port puts its
# own bundled libraries first. Those are built against an old glibc on purpose,
# and a firmware whose Mesa is newer needs symbols they do not carry: on a
# ROCKNIX RG DS, libgallium asked for GLIBCXX_3.4.29 and the bundled libstdc++
# stops at 3.4.28. glvnd discards a vendor whose dlopen fails WITHOUT LOGGING
# IT, so the whole thing surfaces three layers up as eglGetDisplay ->
# EGL_NO_DISPLAY with EGL_BAD_PARAMETER - which reads like a broken EGL and is
# really this port's own search path.
#
# Decided by capability, never by name: try the driver as things stand, and
# only if it fails let the firmware's own copies of the shadowed libraries win
# (the shim directory is already ahead of libs.armhf), then try again. If that
# does not help either, put everything back - a firmware whose libraries are
# OLDER than the bundled set must not be handed them.
gl_mesa_vendor_loads() {
  # No symbol argument: this asks the one question that matters, whether the
  # driver dlopens at all, without assuming which entry points a vendor
  # library exports.
  LD_LIBRARY_PATH="$GL_SHIM:$LD_LIBRARY_PATH" \
    "$GAMEDIR/realracing3" --gl-probe "$GL_MESA_DIR/libEGL_mesa.so.0" >/dev/null 2>&1
}

gl_mesa_unshadow() {
  [ -n "${GL_MESA_DIR:-}" ] || return 0
  [ -e "$GL_MESA_DIR/libEGL_mesa.so.0" ] || return 0
  gl_mesa_vendor_loads && return 0

  echo "GL: the Mesa driver does not load with the bundled libraries in front"
  "$GAMEDIR/realracing3" --gl-probe-deps "$GL_MESA_DIR/libEGL_mesa.so.0" 2>&1 | sed 's/^/GL:   /'

  _unshadowed=""
  for _lib in "$GAMEDIR"/libs.armhf/*.so*; do
    [ -e "$_lib" ] || continue
    _base=$(basename "$_lib")
    [ -e "$GL_MESA_DIR/$_base" ] || continue
    ln -sf "$GL_MESA_DIR/$_base" "$GL_SHIM/$_base" 2>/dev/null &&
      _unshadowed="$_unshadowed $_base"
  done

  if [ -z "$_unshadowed" ]; then
    echo "GL: the firmware carries no replacement for them; the set stays as it is"
    return 1
  fi
  if gl_mesa_vendor_loads; then
    echo "GL: using the firmware's own$_unshadowed so the Mesa driver can load"
    return 0
  fi
  for _base in $_unshadowed; do
    rm -f "$GL_SHIM/$_base"
  done
  echo "GL: the firmware's copies do not help either; bundled libraries kept"
  return 1
}

gl_provider_loadable() {
  local _out _rc _sym
  _sym="${2:-eglGetDisplay}"
  GL_PROBE_REASON=""
  case " $GL_REJECTED " in
    *" $1@$_sym "*) GL_PROBE_REASON="already rejected"; return 1 ;;
  esac
  _out=$("$GAMEDIR/realracing3" --gl-probe "$1" "$_sym" 2>&1)
  _rc=$?
  if [ "$_rc" = 0 ]; then
    echo "GL: preflight ok - $1 loads and resolves $_sym"
    return 0
  fi
  if [ "$_rc" = 3 ]; then
    GL_PROBE_REASON=$(printf '%s' "$_out" | head -n 1)
    GL_REJECTED="$GL_REJECTED $1@$_sym"
    # The first rejection is the one the on-screen message quotes: it is the
    # candidate the search would have committed to before this check existed.
    [ -n "$GL_FIRST_REASON" ] || GL_FIRST_REASON="$GL_PROBE_REASON"
    echo "GL: rejecting $1 - $GL_PROBE_REASON"
    # dlerror() names one missing dependency and stops, so fixing a firmware by
    # that alone is one library per bug report. The audit reads DT_NEEDED out of
    # the candidate and tries each entry, which turns the whole gap into a list
    # this log already contains.
    "$GAMEDIR/realracing3" --gl-probe-deps "$1" 2>&1 | sed 's/^/GL:   /'
    return 1
  fi
  echo "GL: preflight could not run (exit $_rc: $_out); accepting $1 unchecked"
  return 0
}

# Is this library glvnd's vendor-neutral dispatcher rather than a real driver?
#
# The unversioned libEGL.so + libGLESv2.so pair is supposed to identify a split
# Mali wrapper set, because a *runtime* Mesa rootfs ships only the versioned
# sonames. That is true of a console and false of any system where Mesa's -dev
# packages are installed - the build container is one, and there the pair is
# glvnd's, so the wrapper tier would take the case that belongs to tier 4 and
# hand SDL a driver by a path it did not need.
#
# glvnd is recognisable rather than guessed at: every one of its front ends
# links libGLdispatch.so.0, which is where the vendor is registered at runtime.
# A vendor wrapper implements the calls itself and needs no such thing. This is
# the same fact gl_provider_open() in thunks/khronos/gles1.cpp acts on when it
# refuses libGLESv1_CM - a library that resolves every name and implements none
# is worse than one that resolves nothing.
gl_is_glvnd() {
  "$GAMEDIR/realracing3" --gl-probe-deps "$1" 2>&1 | grep -q 'libGLdispatch'
}

# Which of the tiers above answered. It decides how the shim is built and, past
# that, whether SDL is asked for the "mali" video backend.
GL_TIER=""

# Tier 0 - a live Wayland compositor owns the display.
#
# Under a compositor (ROCKNIX runs sway), a vendor blob's EGL cannot join the
# session: it wants the KMS plane the compositor already holds, and dies in
# eglInitialize with EGL_NOT_INITIALIZED after a perfectly clean preflight -
# dlopen and symbol checks cannot see whose display it is (RG-DS report). The
# only EGL that can join a Wayland session is Mesa's, and the firmware that
# runs a compositor ships exactly that (with its 32-bit DRI drivers named by
# the CFW's own LIBGL_DRIVERS_PATH). So: if there is a Wayland socket and a
# Mesa EGL anywhere we search, that pairing outranks every blob tier.
_wayland_socket=""
for _ws in "${XDG_RUNTIME_DIR:-/run/user/0}"/wayland-*; do
  case "$_ws" in *\**) ;; *) [ -e "$_ws" ] && { _wayland_socket="$_ws"; break; } ;; esac
done
if [ -n "$_wayland_socket" ]; then
  for _gldir in $GL_DIRS; do
    [ -e "$_gldir/libEGL_mesa.so.0" ] || continue
    [ -e "$_gldir/libEGL.so.1" ] || continue
    gl_provider_loadable "$_gldir/libEGL.so.1" || continue
    GL_TIER="mesa"
    GL_MESA_DIR="$_gldir"
    echo "GL: Wayland session ($_wayland_socket) + Mesa EGL in $_gldir; blob tiers skipped"
    break
  done
fi

MALI_BLOB=""
gl_try_blob() {
  # A tier already chosen (the Wayland/Mesa rule) is never overridden.
  [ -n "$GL_TIER" ] && return 1
  [ -e "$1" ] || return 1
  gl_provider_loadable "$1" || return 1
  MALI_BLOB="$1"
  GL_TIER="blob"
  return 0
}

# Tier 1 - the exact tested blob filenames.
for candidate in \
  /usr/lib/arm-linux-gnueabihf/libmali-bifrost-g31-rxp0-gbm.so \
  /usr/lib/arm-linux-gnueabihf/libMali.so \
  /usr/lib/arm-linux-gnueabihf/libmali.so.1; do
  gl_try_blob "$candidate" && break
done

# Tier 2 - a split wrapper set. Both halves are probed for the symbol SDL will
# actually call through them, because half a working stack renders nothing.
#
# A third library is looked for beside the pair: whatever in that directory
# answers glMatrixMode. This game is pure GLES 2 - all 142 of its GL imports are
# in the GLES2 table and not one of them is fixed function - so it is not asking
# for fixed function on its own behalf. It is that gl_provider_open() adopts the
# FIRST library that answers glMatrixMode and then serves 58 shared names
# (glClear, glDrawArrays, glTexImage2D...) out of it, ahead of the GLES2 table.
# If that library is not the same driver SDL made the context on, those 58 calls
# run on a second dispatch layer against someone else's state - the exact split
# portbase/AGENTS.md warns about. So: point it at the wrapper set's own driver
# if one is there, and if none is, force single dispatch below rather than let
# it fall through to whatever else on the system happens to export glMatrixMode.
GL_WRAP_EGL=""
GL_WRAP_GLES=""
GL_WRAP_ES1=""
if [ -z "$GL_TIER" ]; then
  for _gldir in $GL_DIRS; do
    [ -d "$_gldir" ] || continue
    [ -e "$_gldir/libEGL.so" ] && [ -e "$_gldir/libGLESv2.so" ] || continue
    if gl_is_glvnd "$_gldir/libEGL.so"; then
      echo "GL: $_gldir/libEGL.so is glvnd's dispatcher, not a vendor wrapper set; leaving this directory to the Mesa tier"
      continue
    fi
    gl_provider_loadable "$_gldir/libEGL.so" || continue
    gl_provider_loadable "$_gldir/libGLESv2.so" glGetString || continue
    GL_WRAP_EGL="$_gldir/libEGL.so"
    GL_WRAP_GLES="$_gldir/libGLESv2.so"
    for _es1 in "$_gldir"/libGLESv1_CM.so "$_gldir"/libGLESv1_CM.so.* \
                "$_gldir"/libmali.so "$_gldir"/libmali.so.* \
                "$_gldir"/libMali.so*; do
      [ -e "$_es1" ] || continue
      # Same reason as above, one library down: adopting glvnd's GLES1 stub
      # would fill the table with pointers that resolve and do nothing.
      gl_is_glvnd "$_es1" && continue
      gl_provider_loadable "$_es1" glMatrixMode || continue
      GL_WRAP_ES1="$_es1"
      break
    done
    GL_TIER="wrapper"
    break
  done
fi

# Tier 3 - any other Mali blob, wherever the distribution put it.
if [ -z "$GL_TIER" ]; then
  for _gldir in $GL_DIRS; do
    [ -d "$_gldir" ] || continue
    for _cand in "$_gldir"/libmali-*.so "$_gldir"/libmali.so.* \
                 "$_gldir"/libmali.so "$_gldir"/libMali.so*; do
      gl_try_blob "$_cand" && break
    done
    [ -n "$MALI_BLOB" ] && break
  done
fi

GL_SHIM="/tmp/realracing3-gl"
rm -rf "$GL_SHIM"
GL_READY=""
GL_PROVIDER=""
if [ -n "$MALI_BLOB" ]; then
  if mkdir -p "$GL_SHIM" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libEGL.so.1" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv1_CM.so.1" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libGLESv2.so.2" \
     && ln -sf "$MALI_BLOB" "$GL_SHIM/libmali.so.1"; then
    GL_READY="y"
    GL_PROVIDER="$MALI_BLOB"
    echo "GL: using Mali blob $MALI_BLOB"
  else
    echo "GL: failed to create /tmp shim, using system libraries"
  fi
elif [ "$GL_TIER" = "wrapper" ]; then
  # SDL is told the two files by path rather than being left to resolve
  # libEGL.so.1 / libGLESv2.so.2 itself: on the firmware this tier is for, the
  # sonames in the library path are the ones that do not work, and the shim
  # cannot outrank a system directory SDL dlopens by absolute name.
  #
  # The shim is still built, under the canonical sonames, because the loader and
  # the game dlopen those directly - SDL_VIDEO_* only reaches SDL.
  if mkdir -p "$GL_SHIM" \
     && ln -sf "$GL_WRAP_EGL" "$GL_SHIM/libEGL.so.1" \
     && ln -sf "$GL_WRAP_GLES" "$GL_SHIM/libGLESv2.so.2"; then
    export SDL_VIDEO_EGL_DRIVER="$GL_WRAP_EGL"
    export SDL_VIDEO_GL_DRIVER="$GL_WRAP_GLES"
    if [ -n "$GL_WRAP_ES1" ]; then
      ln -sf "$GL_WRAP_ES1" "$GL_SHIM/libGLESv1_CM.so.1"
      echo "GL: GLES1-table names will come from $GL_WRAP_ES1 (the wrapper set's own driver)"
    fi
    # "libmali.so.1" is a name other things resolve too, not just our loader:
    # ROCKNIX's mali-hook dlopens it expecting the real blob and pulls the gbm
    # entry points from it. The shim directory is first on the library path,
    # so aliasing the ES1 wrapper under that soname shadowed the blob and
    # killed the whole stack with "undefined symbol:
    # gbm_surface_create_with_modifiers" (RG DS on ROCKNIX). Alias the
    # firmware's own blob when it has one; the ES1 wrapper only answers the
    # name where nothing else does.
    _mali_real=""
    for _gldir in $GL_DIRS; do
      [ -e "$_gldir/libmali.so.1" ] && { _mali_real="$_gldir/libmali.so.1"; break; }
    done
    if [ -n "$_mali_real" ]; then
      ln -sf "$_mali_real" "$GL_SHIM/libmali.so.1"
      echo "GL: libmali.so.1 aliased to the firmware's own blob $_mali_real"
    elif [ -n "$GL_WRAP_ES1" ]; then
      ln -sf "$GL_WRAP_ES1" "$GL_SHIM/libmali.so.1"
    fi
    if [ -z "$GL_WRAP_ES1" ]; then
      # Nothing beside the wrapper set answers glMatrixMode, so gl_provider_open()
      # would keep looking and land on libGL.so.1 - which on a Batocera-derived
      # firmware is gl4es, a whole second GL implementation. It would then serve
      # the 58 shared names while SDL renders through the wrapper set: two
      # dispatch layers, one context, and no error message anywhere.
      #
      # This variable (thunks/khronos/gles1.cpp) resolves that table through
      # SDL_GL_GetProcAddress instead, i.e. through the same driver that owns the
      # context. It costs this game nothing - it has no fixed-function imports
      # for a GLES1-specific provider to serve better.
      #
      # LIBGL_ES is deliberately NOT set here, unlike the sibling port. That
      # variable asks gl4es for a GLES 1.1 backend, which is right for a
      # fixed-function game and wrong for this one: this game's context is
      # GLES 2.0 and its shaders are GLSL ES 1.00. The fix here is to keep gl4es
      # out of the dispatch path, not to configure it.
      export REALRACING3_GL_SINGLE_DISPATCH=1
      echo "GL: no driver beside the wrapper set answers glMatrixMode; resolving the GLES1 table through SDL to keep dispatch on one driver"
    fi
    GL_READY="y"
    GL_PROVIDER="$GL_WRAP_EGL"
    echo "GL: using the 32-bit wrapper set in ${GL_WRAP_EGL%/*} (EGL=$GL_WRAP_EGL GLES=$GL_WRAP_GLES)"
  else
    echo "GL: failed to create /tmp shim for the wrapper set, using system libraries"
  fi
else
  # No unified blob: link whatever 32-bit EGL/GLES entry points exist, each
  # under its own name. libEGL is the one SDL cannot start without.
  GL_EGL=""
  mkdir -p "$GL_SHIM" 2>/dev/null
  # Tier 0 chose a specific Mesa directory; honour it rather than rescanning,
  # or the first loadable libEGL in the search order - which can be the very
  # vendor wrapper the Wayland rule just rejected - wins the shim back.
  if [ -n "${GL_MESA_DIR:-}" ]; then
    for _soname in libEGL.so.1 libGLESv1_CM.so.1 libGLESv2.so.2; do
      [ -e "$GL_MESA_DIR/$_soname" ] && ln -sf "$GL_MESA_DIR/$_soname" "$GL_SHIM/$_soname"
    done
    [ -e "$GL_SHIM/libEGL.so.1" ] && GL_EGL="$GL_MESA_DIR/libEGL.so.1"
  fi
  for _gldir in $GL_DIRS; do
    [ -n "$GL_EGL" ] && break
    # libEGL is what SDL cannot start without, so one directory must provide
    # it and the GLES libraries are taken from that same directory - a set
    # assembled from two userlands would not be one working stack.
    [ -e "$_gldir/libEGL.so.1" ] || continue
    gl_provider_loadable "$_gldir/libEGL.so.1" || continue
    for _soname in libEGL.so.1 libGLESv1_CM.so.1 libGLESv2.so.2; do
      [ -e "$_gldir/$_soname" ] && ln -sf "$_gldir/$_soname" "$GL_SHIM/$_soname"
    done
    [ -e "$GL_SHIM/libEGL.so.1" ] && { GL_EGL="$_gldir/libEGL.so.1"; break; }
  done
  if [ -n "$GL_EGL" ]; then
    GL_READY="y"
    GL_TIER="mesa"
    GL_PROVIDER="$GL_EGL"
    echo "GL: no Mali blob; using the device's 32-bit EGL/GLES set ($GL_EGL)"
    gl_mesa_unshadow
  fi
fi

if [ -n "$GL_READY" ]; then
  export LD_LIBRARY_PATH="$GL_SHIM:$LD_LIBRARY_PATH"

  # Which SDL video backend to ask for.
  #
  # A Batocera-derived firmware carries a vendor "mali" backend that talks to the
  # blob directly; its kmsdrm/x11 defaults are where SDL_CreateWindow dies on
  # those devices, and a Knulli user got this port and its two siblings running
  # by exporting SDL_VIDEODRIVER=mali by hand. Upstream SDL has no such backend,
  # and naming a backend SDL was not built with makes SDL_Init fail outright -
  # which this port treats as fatal - so this is decided by asking SDL what it
  # has, never by firmware name. On a CFW without it the list simply does not
  # contain "mali" and the default is kept, which is why every device working
  # today stays unchanged.
  #
  # The SDL being asked is the SDL the game will use: libSDL2 is deliberately not
  # bundled (tools/collect_libs.sh leaves it to the device), so this binary and
  # the game both link the system libSDL2-2.0.so.0. See src/sdl_info.h.
  #
  # Only on the two Mali tiers. On the Mesa/glvnd tier there is no Mali stack for
  # a "mali" backend to drive.
  if [ "$GL_TIER" = "wrapper" ] || [ "$GL_TIER" = "blob" ]; then
    SDL_INFO=$("$GAMEDIR/realracing3" --sdl-info 2>&1)
    printf '%s\n' "$SDL_INFO" | sed 's/^/GL: /'
    if printf '%s\n' "$SDL_INFO" | grep -q '^sdl: video driver: mali$'; then
      export SDL_VIDEODRIVER=mali
      echo "GL: SDL has a 'mali' video driver and the GL stack is the device's Mali one; selecting SDL_VIDEODRIVER=mali"
    else
      echo "GL: SDL has no 'mali' video driver; keeping SDL default (${SDL_VIDEODRIVER:-unset})"
    fi
  fi
else
  rm -rf "$GL_SHIM"
  echo "GL: no 32-bit GL provider found; searched: $GL_DIRS"
  # Two different firmwares end up here and the fix is not the same, so the
  # screen has to say which one this is. "No driver at all" is a missing
  # package; "a driver that will not load" is a 32-bit dependency the firmware
  # never installed next to it, and that is what a 64-bit userland hits.
  GL_FAIL_WHAT="  This firmware ships no 32-bit Mali
  blob and no 32-bit EGL/GLES set, so
  the game cannot open a window."
  if [ -n "$GL_REJECTED" ]; then
    # The panel is 40 columns at its narrowest, so the screen carries the one
    # word that identifies the problem - the library the driver wanted and did
    # not find - and log.txt carries the whole dlerror() text.
    case "$GL_FIRST_REASON" in
      *"cannot open shared object file"*)
        GL_FAIL_REASON="missing: ${GL_FIRST_REASON%%:*}" ;;
      *)
        GL_FAIL_REASON="$GL_FIRST_REASON" ;;
    esac
    GL_FAIL_WHAT="  A 32-bit GPU driver exists but
  cannot be loaded - its own 32-bit
  libraries are not installed:

    ${GL_FAIL_REASON:0:34}"
  fi
  show_screen 14 <<EOF

  Real Racing 3 - unusable GPU driver

$GL_FAIL_WHAT

  Not starting the game. See log.txt.

EOF
  # And stop here. Starting the loader without a GL provider only replaces this
  # message with a black screen, which reads as a hang and buries the
  # explanation the user had just been shown. This port would in any case die on
  # the SDL_CreateWindow it treats as fatal. show_screen already blocked long
  # enough to read it; return to the frontend instead.
  echo "Not launching the game: there is no GL provider to render with"
  pm_finish
  exit 1
fi

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

# The case a field report would otherwise leave unanswerable: the preflight
# accepted a provider and SDL still could not open a window. That means the
# failure is past dlopen, somewhere in EGL bring-up, and the loader's own
# forensics already walked SDL's default EGL library from inside the failed
# process. Walk the provider the launcher chose too - on a Mali blob those are
# different files, and which of the two comes up is the answer. Done after the
# run so a healthy boot pays nothing.
if [ -n "$GL_PROVIDER" ] && grep -q "SDL_CreateWindow failed" "$GAMEDIR/log.txt"; then
  echo "GL: SDL could not open a window on an accepted provider; auditing $GL_PROVIDER"
  "$GAMEDIR/realracing3" --gl-probe-init "$GL_PROVIDER" 2>&1 | sed 's/^/GL:   /'
  "$GAMEDIR/realracing3" --gl-probe-deps "$GL_PROVIDER" 2>&1 | sed 's/^/GL:   /'
fi

rm -rf /tmp/realracing3-gl
unset LD_LIBRARY_PATH SDL_GAMECONTROLLERCONFIG
unset SDL_VIDEODRIVER SDL_VIDEO_EGL_DRIVER SDL_VIDEO_GL_DRIVER
unset REALRACING3_GL_SINGLE_DISPATCH

pm_finish
exit "$GAME_RC"
