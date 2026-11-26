#!/usr/bin/env python3
"""Pin each pedal's defining behaviour, measured through the real .so.

    tests/test_character.py <build/native dir> <analyze binary>

Small-signal responses come from `analyze resp` (-50 dBFS, so the pedal is
below clipping and the curve is the circuit's); distortion character from
`analyze all`. Each assertion names the circuit fact it protects.
"""
import subprocess
import sys

NATIVE, ANALYZE = sys.argv[1], sys.argv[2]
failures = 0


def run(pedal, *args):
    so = f"{NATIVE}/pfx-{pedal}/pfx-{pedal}.so"
    return subprocess.run([ANALYZE, so, NATIVE, *args],
                          check=True, capture_output=True, text=True).stdout


def resp(pedal, knob, values, **fixed):
    out = run(pedal, "resp", knob, *map(str, values), *[f"{k}={v}" for k, v in fixed.items()])
    lines = out.strip().splitlines()
    freqs = [float(f) for f in lines[0].split()[1:]]
    table = {}
    for line in lines[1:len(values) + 1]:
        cells = line.split()
        table[float(cells[0])] = dict(zip(freqs, map(float, cells[1:])))
    return table


def dist_table(pedal):
    out = run(pedal, "all")
    rows = {}
    for line in out.splitlines()[1:5]:
        c = line.split()
        rows[float(c[0])] = {"pluck": float(c[1]), "thd1k": float(c[3].rstrip("%")),
                             "h2": float(c[4]), "h3": float(c[5]), "eo": float(c[6])}
    return rows


def check(cond, msg):
    global failures
    print(("  ok   " if cond else "  FAIL ") + msg)
    failures += 0 if cond else 1


print("rat")
r = resp("rat", "filter", [0, 1], distortion=0.3)
check(r[0][10000] - r[1][10000] > 15, f"Filter clockwise darkens: 10 kHz {r[0][10000]:+.1f} -> {r[1][10000]:+.1f} dB")
check(abs(r[0][100] - r[1][100]) < 2, "Filter leaves the lows alone (it is a treble cut)")
r = resp("rat", "distortion", [0, 1])
check(r[1][1000] - r[0][1000] > 40, f"Distortion spans > 40 dB of op-amp gain ({r[0][1000]:+.1f} -> {r[1][1000]:+.1f})")
# v0.2: the Filter network sits on the diode node, so a bright Filter loads the
# clipper: the unfiltered small-signal level at 1 kHz drops as Filter opens.
r = resp("rat", "filter", [0, 1], distortion=0)
check(r[1][100] > r[0][100] - 3 and abs(r[0][1000] - r[0][100]) < 1.5, "Filter at 0 is a wide-open RC, flat to 1 kHz")
# v0.3 op-amp switch: OP07DP (RAT2) has ~1.5x the LM308's bandwidth, which
# shows at high gain as more top end; at low gain the two match.
r = resp("rat", "model", [0, 1], distortion=1, filter=0)
check(r[1][5000] - r[0][5000] > 2, f"RAT2 (OP07DP) brighter at full gain: 5 kHz {r[1][5000] - r[0][5000]:+.1f} dB")
r = resp("rat", "model", [0, 1], distortion=0.2, filter=0)
check(abs(r[1][1000] - r[0][1000]) < 0.5, "RAT and RAT2 match at low gain")
d = dist_table("rat")
check(d[0.5]["eo"] < -40, "symmetric 1N914 pair: odd harmonics dominate")

print("ts808")
r = resp("ts808", "drive", [0])
check(r[0][700] - r[0][100] > 8, f"mid hump: 700 Hz is {r[0][700] - r[0][100]:.1f} dB above 100 Hz")
check(r[0][700] - r[0][5000] > 6, "treble falls after the 723 Hz RC")
r = resp("ts808", "tone", [0, 1], drive=0)
check(r[1][3000] - r[0][3000] > 10, f"Tone clockwise brightens: 3 kHz {r[0][3000]:+.1f} -> {r[1][3000]:+.1f}")
r = resp("ts808", "bass", [0, 0.5, 1], drive=0)
check(r[1][100] > r[0.5][100] + 3 and r[0][100] < r[0.5][100] - 3, "Bass: 0.5 stock, left cuts, right restores lows")
check(abs(r[0.5][1000] - r[1][1000]) < 3, "Bass mostly leaves the mids (the C3 mod adds ~2 dB at 1 kHz, as on the pedal)")
d = dist_table("ts808")
check(d[0.5]["thd1k"] < 20, f"overdrive, not distortion: THD {d[0.5]['thd1k']:.0f}% at noon")
check(d[0.5]["h2"] > -80, "diode tolerance leaves faint even harmonics")

print("ds1")
r = resp("ds1", "tone", [0, 0.5, 1], distortion=0, input=0.2)
mid = min(r[0.5][500], r[0.5][700])
# v0.3: the booster high-passes below ~300 Hz (C3 + shunt feedback), so the
# scoop is judged against 200 Hz rather than 100 Hz
check(r[0.5][200] - mid > 2.5 and r[0.5][3000] - mid > 4, f"mid scoop at noon: {r[0.5][200]:+.1f} / {mid:+.1f} / {r[0.5][3000]:+.1f} dB")
r0 = resp("ds1", "tone", [0], distortion=0, input=0.2)[0]
check(r0[300] - r0[60] > 8, f"booster cuts the lows before the clippers: 60 Hz is {r0[300] - r0[60]:.0f} dB under 300 Hz")
check(r[1][5000] - r[0][5000] > 20, f"Tone sweep is large: 5 kHz moves {r[1][5000] - r[0][5000]:.0f} dB")
d = dist_table("ds1")
check(d[0.5]["h3"] > -8, f"hard clipping: H3 at {d[0.5]['h3']:.1f} dB")
check(d[0.5]["eo"] > -30, "the transistor booster adds even harmonics")

print("bd2")
# v0.2, from the stock schematic: the Tone is a PASSIVE treble cut that is flat
# above 1 kHz at noon (C15 18n / TONE 10kB / C16 18n), not a shelf.
r = resp("bd2", "tone", [0, 0.5, 1], gain=0)
hi = [r[0.5][f] for f in (1000, 2000, 5000, 10000)]
check(max(hi) - min(hi) < 1.5, f"Tone at noon is flat above 1 kHz (within {max(hi) - min(hi):.1f} dB)")
check(r[0.5][10000] - r[0][10000] > 12, f"Tone 0 cuts 10 kHz by {r[0.5][10000] - r[0][10000]:.0f} dB")
check(abs(r[0][200] - r[1][200]) < 1.0, "Tone leaves the lows alone")
r = resp("bd2", "gain", [0, 1])
check(r[1][1000] - r[0][1000] > 30, f"ganged Gain spans {r[1][1000] - r[0][1000]:.0f} dB (end-to-end max ~52 dB)")
d = dist_table("bd2")
check(d[0.25]["eo"] > -15, f"asymmetric discrete op-amp: even/odd {d[0.25]['eo']:+.1f} dB at low gain")
check(d[0.25]["thd1k"] < d[1.0]["thd1k"] / 2, "low gain much cleaner than full gain")

print("mt2")
r = resp("mt2", "mid", [0, 1], distortion=0)
check(r[1][1000] - r[0][1000] > 25, f"Mid +-15 dB: {r[1][1000] - r[0][1000]:.0f} dB span at 1 kHz")
r = resp("mt2", "mid_freq", [0, 1], distortion=0, mid=1)
flat = resp("mt2", "mid", [0.5], distortion=0)[0.5]
lo = max(r[0], key=lambda f: r[0][f] - flat[f])
hi = max(r[1], key=lambda f: r[1][f] - flat[f])
check(lo <= 300 and hi >= 3000, f"Mid Freq sweeps: peak at {lo:.0f} Hz -> {hi:.0f} Hz")
r = resp("mt2", "high", [0, 1], distortion=0)
check(r[1][10000] - r[0][10000] > 25, "High +-15 dB")
r = resp("mt2", "low", [0, 0.1, 0.5, 0.9, 1], distortion=0)
check(r[1][100] - r[0][100] > 32, f"Low +-19 dB at the stops: span {r[1][100] - r[0][100]:.0f} dB")
check(abs((r[0.9][100] - r[0.5][100]) - 7) < 2.5, f"Low pot law: 90 % is +{r[0.9][100] - r[0.5][100]:.1f} dB (circuit: ~+7)")
r = resp("mt2", "distortion", [0])
pk = max(r[0], key=r[0].get)
check(700 <= pk <= 1500, f"pre-distortion mid hump peaks at {pk:.0f} Hz (circuit: ~900 Hz)")
d = dist_table("mt2")
check(d[0.5]["thd1k"] > 30, f"high gain at noon: THD {d[0.5]['thd1k']:.0f}%")

print("mix")
# The wet path must not cancel the dry one at Mix 50 % (polarity and the dry
# alignment delay). Before the fix: TS808 -17 dB at 4 kHz, BD-2 -14 dB at 7 kHz.
for p, k in (("rat", "distortion"), ("ts808", "drive"), ("ds1", "distortion"), ("bd2", "gain"), ("mt2", "distortion")):
    r = resp(p, "mix", [0.5], **{k: 0})
    worst = min(v for f, v in r[0.5].items() if 100 <= f <= 10000)
    check(worst > -6.0, f"{p}: Mix 50 % never dips below {worst:+.1f} dB (no cancellation)")

print("PASS" if not failures else f"FAILED: {failures}")
sys.exit(1 if failures else 0)
