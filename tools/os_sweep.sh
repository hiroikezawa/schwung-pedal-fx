#!/usr/bin/env bash
# Alias floor and CPU of every pedal at 1x / 2x / 4x oversampling.
#   tools/os_sweep.sh [pedal ...]      (needs build/tests/analyze)
# alias = non-harmonic / harmonic energy of a -12 dBFS sine at knob-1 = 0.5 / 1.0
set -euo pipefail
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
PEDALS=("$@"); [[ ${#PEDALS[@]} -gt 0 ]] || PEDALS=(rat ts808 ds1 bd2 mt2)
A="$PROJECT/build/tests/analyze"
TMP="$PROJECT/build/os_sweep"; mkdir -p "$TMP"
printf "%-6s %3s %9s %9s %9s %9s %10s\n" pedal os "a1k@.5" "a5k@.5" "a1k@1" "a5k@1" "us/block"
for p in "${PEDALS[@]}"; do
  id=$(python3 -c "import json; print(json.load(open('$PROJECT/src/$p/pedal.json'))['id'])")
  for os in 1 2 4; do
    PFX_OS_OVERRIDE=$os bash "$PROJECT/scripts/build.sh" native "$p" >/dev/null
    so="$TMP/$id-$os.so"; cp "$PROJECT/build/native/$id/$id.so" "$so"
    read -r a1 a5 b1 b5 < <("$A" "$so" "$TMP" all | awk '$1=="0.50"{a=$8" "$9} $1=="1.00"{b=$8" "$9} END{print a, b}')
    us=$("$A" "$so" x cpu | awk 'NR==1{print $3}')
    printf "%-6s %3s %9s %9s %9s %9s %10s\n" "$p" "${os}x" "$a1" "$a5" "$b1" "$b5" "$us"
  done
  bash "$PROJECT/scripts/build.sh" native "$p" >/dev/null   # restore the shipped factor
done
