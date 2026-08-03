#!/usr/bin/env bash
#
# Interactive, host-controlled Real Racing 3 emulator.
#
# The game still runs through qemu-arm and Mesa/llvmpipe, as in the immutable
# verifier, but it has no fixed frame limit and mounts a bidirectional control
# directory. emulator_control.cpp consumes commands from that directory and
# writes PNG screenshots/status back to it.
set -euo pipefail

PORT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME_DIR="${REALRACING3_GAMEDIR:-$PORT_DIR/../.local-donor}"
CONTROL_DIR="${REALRACING3_CONTROL_DIR:-$PORT_DIR/emulator/runtime}"
IMAGE="${REALRACING3_BUILD_IMAGE:-realracing3-build}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --game-dir)
            GAME_DIR="${2:?--game-dir needs a path}"
            shift 2
            ;;
        --control-dir)
            CONTROL_DIR="${2:?--control-dir needs a path}"
            shift 2
            ;;
        *)
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

# QEMU_STRACE is forwarded only when it has a value. Passing "-e QEMU_STRACE="
# defines the variable as empty, and qemu-user enables its syscall trace on the
# variable being *present* - so the previous unconditional form logged every
# syscall of every run. Measured on a 120-frame run: 45420 log lines, of which
# 37000+ were strace, and the game's own trace was buried in them.
if [ ! -f "$GAME_DIR/lib/armeabi-v7a/libRealRacing3.so" ]; then
    echo "game tree not found at $GAME_DIR" >&2
    exit 2
fi

mkdir -p "$CONTROL_DIR/screenshots"
# Shader dumps land under the control mount because it is the only rw bind
# besides the game tree, which is the player's data and stays untouched.
mkdir -p "$CONTROL_DIR/shaders"
: > "$CONTROL_DIR/commands"
rm -f "$CONTROL_DIR/status.json" "$CONTROL_DIR/status.json.tmp"

if [ "${REALRACING3_EMULATOR_SKIP_BUILD:-0}" != "1" ]; then
    docker run --rm \
        -v "$PORT_DIR":/src \
        -w /src \
        "$IMAGE" make -j4
fi

exec docker run --rm \
    -v "$PORT_DIR":/src \
    -v "$GAME_DIR":/game:rw \
    -v "$CONTROL_DIR":/control \
    -w /game \
    -e SDL_VIDEODRIVER=offscreen \
    -e SDL_AUDIODRIVER=dummy \
    -e LIBGL_ALWAYS_SOFTWARE=1 \
    -e GALLIUM_DRIVER=llvmpipe \
    -e EGL_PLATFORM=surfaceless \
    -e LOADER_TRACE=1 \
    ${QEMU_STRACE:+-e QEMU_STRACE="$QEMU_STRACE"} \
    -e REALRACING3_AUTOPILOT="${REALRACING3_AUTOPILOT:-}" \
    -e REALRACING3_FRAME_LIMIT="${REALRACING3_FRAME_LIMIT:-}" \
    -e REALRACING3_SKIP_RENDER="${REALRACING3_SKIP_RENDER:-}" \
    -e REALRACING3_NO_VFP_PATCH="${REALRACING3_NO_VFP_PATCH:-}" \
    -e REALRACING3_VFP_SELFTEST="${REALRACING3_VFP_SELFTEST:-}" \
    -e REALRACING3_CONTROL_DIR=/control \
    -e REALRACING3_GL_DIAG="${REALRACING3_GL_DIAG:-}" \
    -e REALRACING3_GL_STATS="${REALRACING3_GL_STATS:-}" \
    -e REALRACING3_GL_STATS_EVERY="${REALRACING3_GL_STATS_EVERY:-}" \
    -e REALRACING3_GL_STATS_ERRORS="${REALRACING3_GL_STATS_ERRORS:-}" \
    -e REALRACING3_SHADER_DUMP_DIR="${REALRACING3_SHADER_DUMP_DIR:-}" \
    -e REALRACING3_SKIP_COMPOSITE_QUAD="${REALRACING3_SKIP_COMPOSITE_QUAD:-}" \
    -e REALRACING3_MOTION_TRACE_ALL="${REALRACING3_MOTION_TRACE_ALL:-}" \
    -e REALRACING3_MOTION_NO_GATE="${REALRACING3_MOTION_NO_GATE:-}" \
    -e REALRACING3_CAMERA_NO_SWIPE="${REALRACING3_CAMERA_NO_SWIPE:-}" \
    -e REALRACING3_TRIGGER_ACCEL="${REALRACING3_TRIGGER_ACCEL:-}" \
    -e REALRACING3_CONTROL_METHOD="${REALRACING3_CONTROL_METHOD:-}" \
    -e REALRACING3_TUTORIAL_TRACE="${REALRACING3_TUTORIAL_TRACE:-}" \
    -e REALRACING3_FORCE_SOFTWARE_TEXTURE_DECODE="${REALRACING3_FORCE_SOFTWARE_TEXTURE_DECODE:-}" \
    -e REALRACING3_DISABLE_FRAMEBUFFER_FETCH="${REALRACING3_DISABLE_FRAMEBUFFER_FETCH:-1}" \
    -e REALRACING3_DISABLE_PROGRAM_BINARIES="${REALRACING3_DISABLE_PROGRAM_BINARIES:-1}" \
    -e REALRACING3_S3TC_DXT3_OFF="${REALRACING3_S3TC_DXT3_OFF:-}" \
    -e REALRACING3_CURSOR_DIAG="${REALRACING3_CURSOR_DIAG:-}" \
    -e REALRACING3_CURSOR_RESTORE="${REALRACING3_CURSOR_RESTORE:-}" \
    -e REALRACING3_EGL_PRESERVE="${REALRACING3_EGL_PRESERVE:-}" \
    "$IMAGE" \
    qemu-arm -L /usr/arm-linux-gnueabihf \
        "${REALRACING3_BINARY:-/src/build/realracing3}" /game
