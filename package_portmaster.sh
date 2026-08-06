#!/usr/bin/env bash
#
# Build a game-data-free PortMaster package for the Real Racing 3 port.
#
# Every check below exists because its absence already produced a broken zip
# once: a libs.armhf/ copied by hand (no MANIFEST, two libraries short of the
# transitive closure), and a launcher whose PortMaster signature no longer
# matched the release filename.
set -euo pipefail

cd "$(dirname "$0")"

OUT="build/realracing3-portmaster.zip"
STAGE="build/pkg-portmaster"

[ -x build/realracing3 ] \
    || { echo "build/realracing3 missing - run make first" >&2; exit 1; }
# MANIFEST.txt and licenses/ only exist when tools/collect_libs.sh produced the
# directory. A hand-copied libs.armhf/ has neither, and is how the port shipped
# without libcrypto.so.1.1 and libbz2.so.1.0 (both needed by libzip.so.4).
[ -f build/libs.armhf/MANIFEST.txt ] \
    || { echo "build/libs.armhf missing or hand-made - run make libs first" >&2; exit 1; }
[ -d build/libs.armhf/licenses ] \
    || { echo "build/libs.armhf/licenses missing - run make libs again" >&2; exit 1; }

rm -rf "$STAGE"
rm -f "$OUT"
mkdir -p "$STAGE/realracing3"

# BYOG: the zip ships ONLY the loader, launcher and metadata (like the sibling
# ports, a few MB). Game data is NOT bundled - a multi-GB zip stalls
# PortMaster's on-device installer, and redistributing it would be piracy. The
# user copies their own tree into ports/realracing3/; the loader's
# assets-fallback (fix_path) handles the flat layout.
cp "ports/Real Racing 3.sh"                       "$STAGE/"
cp build/realracing3                              "$STAGE/realracing3/"
cp ports/realracing3/realracing3.gptk             "$STAGE/realracing3/"
cp ports/realracing3/port.json                    "$STAGE/realracing3/"
cp ports/realracing3/gameinfo.xml                 "$STAGE/realracing3/"
# The artwork PortMaster merges into the frontend's game list when it installs
# the zip. Without them the port installs correctly and then appears as a bare
# filename with no image - which is exactly how this one looked, because
# gameinfo.xml never named an image and neither file existed.
cp ports/realracing3/cover.png                    "$STAGE/realracing3/"
cp ports/realracing3/screenshot.png               "$STAGE/realracing3/"
cp ports/realracing3/README.md                    "$STAGE/realracing3/"
cp ports/realracing3/CREDITS.md                   "$STAGE/realracing3/"
cp ports/realracing3/PUT_REAL_RACING_3_DATA_HERE.txt "$STAGE/realracing3/"
cp ports/realracing3/realracing3.eapx.json        "$STAGE/realracing3/"
cp tools/eapx.py                                  "$STAGE/realracing3/"
cp -R build/libs.armhf                            "$STAGE/realracing3/"

# PortMaster releases carry the copyright terms of everything they
# redistribute. collect_libs.sh already produced one file per bundled .so;
# move them next to the port's own licences so a user finds all of them in one
# place.
mkdir -p "$STAGE/realracing3/licenses/libraries"
cp LICENSE   "$STAGE/realracing3/licenses/LICENSE-portmaster-port.txt"
cp NOTICE.md "$STAGE/realracing3/licenses/NOTICE.md"
cp third_party/gmloader/LICENSE.md "$STAGE/realracing3/licenses/LICENSE-gmloader.md"
cp third_party/powervr/LICENSE.md  "$STAGE/realracing3/licenses/LICENSE-powervr.txt"
# stb_truetype is not a bundled .so, so collect_libs.sh never sees it - but it is
# compiled into the loader and therefore redistributed as object code, and its
# MIT alternative requires the notice to travel with the binary.
cp third_party/stb/LICENSE.md      "$STAGE/realracing3/licenses/LICENSE-stb.md"
mv "$STAGE/realracing3/libs.armhf/licenses/"* "$STAGE/realracing3/licenses/libraries/"
rmdir "$STAGE/realracing3/libs.armhf/licenses"

chmod +x "$STAGE/Real Racing 3.sh" "$STAGE/realracing3/realracing3" \
         "$STAGE/realracing3/eapx.py"

# The parentheses matter: without them -delete binds to the last -name only and
# AppleDouble files copied from a macOS volume survive into the zip.
find "$STAGE" \( -name '._*' -o -name '.DS_Store' \) -delete
mkdir -p "$(dirname "$OUT")"
(cd "$STAGE" && zip -qr "../../$OUT" .)

# PortMaster rewrites an unsigned root launcher in place to add this line. Its
# implementation opens the file with mode "w" before writing, so an interrupted
# install can leave a zero-byte launcher on exFAT. Ship the canonical signature
# ourselves: with the release filename below, PortMaster recognizes it and does
# not touch the launcher after extraction.
EXPECTED_SIGNATURE="# PORTMASTER: realracing3-portmaster.zip, Real Racing 3.sh"
ACTUAL_SIGNATURE="$(unzip -p "$OUT" "Real Racing 3.sh" | sed -n '2p')"
[ "$ACTUAL_SIGNATURE" = "$EXPECTED_SIGNATURE" ] || {
    echo "package has wrong PortMaster signature: $ACTUAL_SIGNATURE" >&2
    exit 1
}
[ "$(unzip -p "$OUT" "Real Racing 3.sh" | wc -c | tr -d ' ')" -gt 0 ] || {
    echo "package has an empty launcher" >&2
    exit 1
}
unzip -tq "$OUT" >/dev/null

# The packaged eapx must be the canonical one. An earlier port shipped 0.2.0
# while the source tree was already at 0.4.1, because nobody compared them - the
# copy in tools/ is easy to forget and impossible to notice from the outside.
canonical="${EAPX_CANONICAL:-$HOME/Projects/Others/handheld/eapx/eapx.py}"
if [ -f "$canonical" ]; then
  if ! cmp -s tools/eapx.py "$canonical"; then
    echo "refusing package: tools/eapx.py differs from the canonical $canonical" >&2
    echo "  packaged:  $(sed -n 's/^VERSION = "\(.*\)"/\1/p' tools/eapx.py)" >&2
    echo "  canonical: $(sed -n 's/^VERSION = "\(.*\)"/\1/p' "$canonical")" >&2
    exit 1
  fi
else
  echo "note: canonical eapx not found at $canonical; packaged copy not verified" >&2
fi

listing="$(unzip -Z1 "$OUT")"
for required in "Real Racing 3.sh" "realracing3/realracing3" \
                "realracing3/realracing3.gptk" "realracing3/port.json" \
                "realracing3/gameinfo.xml" "realracing3/README.md" \
                "realracing3/cover.png" "realracing3/screenshot.png" \
                "realracing3/CREDITS.md" \
                "realracing3/PUT_REAL_RACING_3_DATA_HERE.txt" \
                "realracing3/eapx.py" \
                "realracing3/realracing3.eapx.json" \
                "realracing3/licenses/LICENSE-portmaster-port.txt" \
                "realracing3/licenses/LICENSE-powervr.txt" \
                "realracing3/licenses/LICENSE-stb.md" \
                "realracing3/licenses/libraries/libstdc++.so.6.copyright" \
                "realracing3/libs.armhf/MANIFEST.txt" \
                "realracing3/libs.armhf/libcrypto.so.1.1" \
                "realracing3/libs.armhf/libbz2.so.1.0"; do
    case "$listing" in
        *"$required"*) ;;
        *) echo "package missing $required" >&2; exit 1 ;;
    esac
done

# Bring your own game: nothing from the donor may ever reach the release.
case "$listing" in
    *libRealRacing3.so*|*libfmodex.so*|*asset_list_*|*assets_480x320*)
        echo "refusing package: proprietary game data found" >&2
        exit 1
        ;;
esac

# The zip must carry the binary that was just built, not whatever was lying in
# the staging directory. This has bitten the project twice: a package built
# from a stale stage would have shipped a port whose text never rendered, and
# nothing in the checks above would have noticed - they all pass on an old
# binary. Comparing the hashes is the only check that catches it.
built_sha="$(shasum -a 256 build/realracing3 | cut -d' ' -f1)"
packed_sha="$(unzip -p "$OUT" realracing3/realracing3 | shasum -a 256 | cut -d' ' -f1)"
[ "$built_sha" = "$packed_sha" ] || {
    echo "refusing package: the zipped binary is not the one just built" >&2
    echo "  built:  $built_sha" >&2
    echo "  packed: $packed_sha" >&2
    exit 1
}

echo "$OUT"
echo "binary sha256: $built_sha"
du -h "$OUT"
