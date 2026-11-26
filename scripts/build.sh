#!/usr/bin/env bash
# Build the pedal audio FX.
#
#   scripts/build.sh [arm64|native] [pedal ...]
#
#   arm64   (default) Move .so files + install tarballs in dist/
#   native  desktop .so files in build/native/ for the tests
#   pedal   rat ts808 ds1 bd2 mt2 (default: all five)
#
# Each pedal is ONE translation unit: src/<pedal>/<pedal>.c includes the
# shared headers in src/common/ and, last, the Schwung v2 adapter. The
# manifest and C metadata are generated from src/<pedal>/pedal.json.
set -euo pipefail
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
SCHWUNG_ROOT="${SCHWUNG_ROOT:-$PROJECT/../schwung}"
ALL=(rat ts808 ds1 bd2 mt2)

ARCH="${1:-arm64}"
case "$ARCH" in
  arm64)  CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"; TUNE=(-march=armv8-a -mtune=cortex-a72) ;;
  native) CC="${CC:-gcc}"; TUNE=() ;;
  *) echo "usage: $0 [arm64|native] [pedal ...]" >&2; exit 2 ;;
esac
shift || true
PEDALS=("$@")
[[ ${#PEDALS[@]} -gt 0 ]] || PEDALS=("${ALL[@]}")

[[ -f "$SCHWUNG_ROOT/src/host/audio_fx_api_v2.h" ]] || {
  echo "Schwung source not found at $SCHWUNG_ROOT (set SCHWUNG_ROOT)" >&2; exit 1; }

# No -ffast-math: the isfinite() guards must survive.
CFLAGS=(-std=c11 -D_DEFAULT_SOURCE -O2 -fPIC -fno-math-errno -ffp-contract=fast -fvisibility=hidden
        -Wall -Wextra -Werror -Wshadow -Wno-unused-function "${TUNE[@]}")
# PFX_EXTRA_CFLAGS / PFX_NATIVE_DIR: used by scripts/test.sh for the sanitizer build.
read -r -a EXTRA <<< "${PFX_EXTRA_CFLAGS:-}"
CFLAGS+=("${EXTRA[@]}")

for pedal in "${PEDALS[@]}"; do
  [[ " ${ALL[*]} " == *" $pedal "* ]] || { echo "unknown pedal: $pedal (${ALL[*]})" >&2; exit 2; }
  src="$PROJECT/src/$pedal"
  gen="$PROJECT/build/gen/$pedal"
  python3 "$PROJECT/scripts/gen_meta.py" "$src" "$gen"
  id=$(python3 -c "import json; print(json.load(open('$src/pedal.json'))['id'])")
  os=$(python3 -c "import json; print(json.load(open('$src/pedal.json'))['oversampling'])")
  license=$(python3 -c "import json; print(json.load(open('$src/pedal.json'))['license'])")
  if [[ "$ARCH" == arm64 ]]; then OUT="$PROJECT/dist/$id"; else OUT="${PFX_NATIVE_DIR:-$PROJECT/build/native}/$id"; fi
  rm -rf "$OUT"; mkdir -p "$OUT"
  "$CC" "${CFLAGS[@]}" -DPFX_OS="${PFX_OS_OVERRIDE:-$os}" -I"$PROJECT/src" -I"$SCHWUNG_ROOT/src" -I"$gen" \
    -shared -Wl,-z,defs -Wl,-z,nodelete "$src/$pedal.c" -o "$OUT/$id.so" -lm
  cp "$gen/module.json" "$src/help.json" "$OUT/"
  if [[ "$ARCH" == arm64 ]]; then
    python3 "$PROJECT/scripts/check_abi.py" "$OUT/$id.so"
    cp "$src/README.md" "$PROJECT/NOTICE" "$PROJECT/THIRD_PARTY_LICENSES.md" "$OUT/"
    case "$license" in
      GPL-3.0-only) cp "$PROJECT/LICENSE-GPL-3.0" "$OUT/LICENSE" ;;
      MIT)          cp "$PROJECT/LICENSE" "$OUT/LICENSE" ;;
      *) echo "unknown license $license" >&2; exit 1 ;;
    esac
    tar -czf "$PROJECT/dist/$id-module.tar.gz" -C "$PROJECT/dist" "$id"
    echo "[$pedal] -> dist/$id-module.tar.gz ($(du -h "$PROJECT/dist/$id-module.tar.gz" | cut -f1))"
  else
    echo "[$pedal] -> ${OUT#$PROJECT/}/$id.so"
  fi
done
