#!/usr/bin/env bash
# Choose each pedal's dry delay for the Mix knob (PFX_DRY_DELAY): build with
# 0..12 samples, measure the small-signal response at Mix 50 % with the drive knob at
# 0 and the defaults otherwise, and report the deepest dip (100 Hz - 10 kHz)
# relative to the dry level. A dip is the wet signal cancelling the dry one.
#   tools/mix_align.sh [pedal ...]
set -euo pipefail
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
PEDALS=("$@"); [[ ${#PEDALS[@]} -gt 0 ]] || PEDALS=(rat ts808 ds1 bd2 mt2)
SCHWUNG_ROOT="${SCHWUNG_ROOT:-$PROJECT/../schwung}"
TMP="$PROJECT/build/mix_align"; mkdir -p "$TMP"
for p in "${PEDALS[@]}"; do
  bash "$PROJECT/scripts/build.sh" native "$p" >/dev/null
  id=$(python3 -c "import json; print(json.load(open('$PROJECT/src/$p/pedal.json'))['id'])")
  os=$(python3 -c "import json; print(json.load(open('$PROJECT/src/$p/pedal.json'))['oversampling'])")
  k=$(python3 -c "import json; print(next(q['key'] for q in json.load(open('$PROJECT/src/$p/pedal.json'))['params'] if q['key'] in ('drive','distortion','gain')))")
  printf "%-6s" "$p"
  for d in 0 1 2 3 4 5 6 7 8 10 12; do
    gcc -std=c11 -D_DEFAULT_SOURCE -O2 -fPIC -shared -fno-math-errno -ffp-contract=fast -fvisibility=hidden \
      -DPFX_OS="$os" -DPFX_DRY_DELAY_OVERRIDE="$d" -I"$PROJECT/src" -I"$SCHWUNG_ROOT/src" -I"$PROJECT/build/gen/$p" \
      "$PROJECT/src/$p/$p.c" -o "$TMP/$id-$d.so" -lm
    worst=$("$PROJECT/build/tests/analyze" "$TMP/$id-$d.so" x resp mix 0.5 "$k=0" | awk 'NR==2{w=99; for(i=5;i<=19;i++) if($i<w) w=$i; print w}')
    printf "  d%-2s %6s" "$d" "$worst"
  done
  echo
done
