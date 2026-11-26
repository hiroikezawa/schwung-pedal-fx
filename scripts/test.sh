#!/usr/bin/env bash
# Every test, then the Move build:
#   1. DSP unit tests + circuit cross-check          tests/test_common.c
#   2. RAT port vs dm-Rat's own test vectors         tests/test_rat_port.c
#   3. each .so through the real v2 ABI              tests/test_pedal.c
#      (also under AddressSanitizer + UBSan)
#   4. each pedal's defining circuit behaviour       tests/test_character.py
#   5. ARM64 build + ABI check (glibc <= 2.34, one export)
set -euo pipefail
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
SCHWUNG_ROOT="${SCHWUNG_ROOT:-$PROJECT/../schwung}"
cd "$PROJECT"
T=build/tests; mkdir -p "$T"
CC="${CC:-gcc}"
PEDALS=(rat ts808 ds1 bd2 mt2)

echo "== native build"
bash scripts/build.sh native
"$CC" -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -Isrc tests/test_common.c -o $T/test_common -lm
"$CC" -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Werror -DPFX_OS=4 -Isrc -I"$SCHWUNG_ROOT/src" -Ibuild/gen/rat \
  tests/test_rat_port.c -o $T/test_rat_port -lm
"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -I"$SCHWUNG_ROOT/src" tests/test_pedal.c -o $T/test_pedal -ldl -lm
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -I"$SCHWUNG_ROOT/src" tools/analyze.c -o $T/analyze -ldl -lm

echo "== DSP unit tests";            $T/test_common
echo "== RAT port";                  $T/test_rat_port
for p in "${PEDALS[@]}"; do
  id=$(python3 -c "import json; print(json.load(open('src/$p/pedal.json'))['id'])")
  echo "== $p (v2 ABI)";             $T/test_pedal build/native/$id/$id.so | grep -E "FAIL|PASS"
done

echo "== sanitizers (ASan + UBSan)"
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
PFX_EXTRA_CFLAGS="${SAN[*]} -g -O1" PFX_NATIVE_DIR="$PROJECT/build/asan" bash scripts/build.sh native >/dev/null
"$CC" -std=c11 -O1 -g "${SAN[@]}" -I"$SCHWUNG_ROOT/src" tests/test_pedal.c -o $T/test_pedal_asan -ldl -lm
for p in "${PEDALS[@]}"; do
  id=$(python3 -c "import json; print(json.load(open('src/$p/pedal.json'))['id'])")
  printf "  %-6s " "$p"; ASAN_OPTIONS=detect_leaks=1 $T/test_pedal_asan build/asan/$id/$id.so | tail -1
done

echo "== Schwung page planner / contract validator"
if command -v node >/dev/null 2>&1; then
  node tests/test_ui.mjs "$SCHWUNG_ROOT" "$PROJECT"
elif command -v node.exe >/dev/null 2>&1 && command -v wslpath >/dev/null 2>&1; then
  node.exe "$(wslpath -w tests/test_ui.mjs)" "$(wslpath -w "$SCHWUNG_ROOT")" "$(wslpath -w "$PROJECT")"
else
  echo "SKIP: Node.js not found (UI contract not checked)" >&2
fi

echo "== circuit character";         python3 tests/test_character.py build/native $T/analyze

echo "== ARM64"
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" >/dev/null 2>&1; then
  bash scripts/build.sh arm64
else
  echo "SKIP: no aarch64 cross compiler" >&2
fi
echo "ALL TESTS PASSED"
