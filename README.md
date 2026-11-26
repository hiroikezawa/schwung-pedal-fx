# schwung-pedal-fx

**English** | [日本語](README.ja.md)

The circuits of five guitar pedals, **modelled from the circuit equations**, as
Audio FX for Schwung on Ableton Move. All five are ordinary Schwung Audio FX
(Audio FX API v2, `component_type: audio_fx`, `chainable`) and can be added from
**Schwung → Signal Chain → Audio FX**. One FX = one module (Schwung's chain host
loads `modules/audio_fx/<id>/<id>.so`), sharing common DSP code.

> **These are unofficial emulations.** They have no connection with, and are not
> endorsed by, BOSS / Roland, Ibanez (Hoshino Gakki), Maxon, ProCo Sound / Radial
> Engineering or Ableton. RAT, TS808, Tube Screamer, DS-1, BD-2, Blues Driver,
> MT-2 and Metal Zone are trademarks of their owners and are used only to identify
> the circuits studied. The published names do not use brand names.

| Name on Move | id | Circuit studied | Implementation | Licence | OS | CPU (host, stereo) |
|---|---|---|---|---|---|---|
| **Rat Drive** | `pfx-rat` | RAT | **Port** from dm-Rat → schwung-303, plus extensions | **GPL-3.0-only** | 4x | 39.4 µs/block |
| **Green Screamer** | `pfx-ts808` | TS808 | **New implementation** from schematics and papers | MIT | 2x | 26.2 µs |
| **Orange Distortion** | `pfx-ds1` | DS-1 | **New implementation** from the schematic | MIT | 4x | 69.0 µs |
| **Blues Drive** | `pfx-bd2` | BD-2 | **New implementation** from circuit analysis | MIT | 2x | 58.2 µs |
| **Metal Sweep** | `pfx-mt2` | MT-2 | **New implementation** from circuit analysis | MIT | 4x | 32.2 µs |

It is called **Metal Sweep** rather than "Metal Zone-style" because "Metal Zone" is
BOSS's own product name (the name can be changed in the single `name` line of
`src/mt2/pedal.json`).

## Layout

```
schwung-pedal-fx/
  src/common/        shared DSP (MIT)
    filters.h          one-pole/biquad, s-domain → z-domain (bilinear transform + prewarp), shelf/peak
    oversampling.h     2x/4x polyphase IIR half-band (99 dB / 100 dB)
    smoothing.h        control-rate smoothing (16 frames) + audio-rate ramps
    nonlinear.h        closed-form diode-pair solution (Wright omega), capacitor companion models, rail limiter
    tapers.h           A/C/S curves, log frequency, dB knobs, Level taper
    plugin_v2.h        Schwung Audio FX v2 adapter (Input/Mix/Output, bypass, NaN protection, presets, state)
  src/<pedal>/       rat ts808 ds1 bd2 mt2
    <pedal>.c          circuit model (one translation unit, including plugin_v2.h at the end)
    pedal.json         single source of knobs, defaults and presets → generates module.json and a C header
    help.json          on-device help (Module Help)
    README.md          the English README shipped with each module
  scripts/           build.sh  test.sh  install.sh / install.py  gen_meta.py  check_abi.py
  tests/             test_common.c  test_rat_port.c  test_pedal.c  test_character.py
  tools/             analyze.c (WAV, THD, spectra, frequency response, CPU)  os_sweep.sh  design_halfband.py
  docs/MEASUREMENTS.md   measurement results
  LICENSE (MIT + the RAT exception)  LICENSE-GPL-3.0  NOTICE  THIRD_PARTY_LICENSES.md
```

## Building, testing, installing

Needs `gcc`, `aarch64-linux-gnu-gcc` and Python 3 (standard library only) on
WSL/Linux. Schwung's source is expected next to this repository (`../schwung`;
change it with `SCHWUNG_ROOT`).

```bash
bash scripts/test.sh              # all tests (including ASan/UBSan) → ARM64 build → ABI check
bash scripts/build.sh arm64       # the five dist/<id>-module.tar.gz
bash scripts/build.sh arm64 ds1   # just one
bash scripts/install.sh http://move.local:7700 --check   # connection and package check only
bash scripts/install.sh http://move.local:7700           # all five to the Move
bash scripts/install.sh http://move.local:7700 rat mt2   # only the ones named
```

Installation uses Schwung Manager's "Custom install" (`POST /modules/install-custom`),
so no SSH is needed. They go into `modules/audio_fx/<id>/`. A `.so` that is not
ARM64 is refused before upload. The ARM64 `.so` uses only glibc 2.29 symbols or
older (Move has 2.34), and its only public symbol is `move_audio_fx_init_v2`
(`scripts/check_abi.py`).

## Knobs (conventions shared by every pedal)

- Every knob value is **0.0–1.0** (shown as 0–100 % on Move) and converted to real
  values inside the DSP.
- **Level / Volume at 50 % = the same level as the input** (calibrated with default
  settings on a plucked phrase peaking at -6 dBFS). ±20 dB, silent at 0 %. A dB
  scale (an idealised A curve).
- **Input** (where present): input to the pedal ±24 dB, 50 % = 0 dB. **Output**:
  after Mix ±18 dB, 50 % = 0 dB.
- **Mix**: an equal-power crossfade. **Mix 0 % + Output 50 % is a bit-identical
  bypass that runs no DSP at all.**
- Drive/Gain: A curve (logarithmic); Frequency: logarithmic; Tone: a curve matched
  to the circuit (an S/G curve for the TS, and so on).
- Smoothing: Drive/Gain 5 ms (so the response is not dulled), Tone/EQ 10 ms, Mid
  Freq 20 ms, levels 15 ms.
- Input voltage reference: digital full scale = 0.5 V at the circuit input (12 dB
  lower for Rat Drive only). A Move track peaking at -6 dBFS enters the circuit at
  250 mV, like a hot humbucker. If that is too strong for synths or drums, lower
  Input.

| # | Rat Drive | Green Screamer | Orange Distortion | Blues Drive | Metal Sweep |
|---|---|---|---|---|---|
| 1 | Mix | Mix | Mix | Mix | Mix |
| 2 | Distortion | Drive | Distortion | Gain | Distortion |
| 3 | Filter (darker clockwise) | Tone | Tone | Tone (passive treble cut) | Low ±19 dB |
| 4 | Low Cut 20 Hz–1 kHz | Bass (50 % = the original) | Bass ±10 dB | Bass ±9 dB | High ±20 dB |
| 5 | Hi Cut off–1 kHz | Presence ±9 dB | Presence ±9 dB | Presence ±9 dB | Mid ±15 dB |
| 6 | Input | Input | Input | Input | Mid Freq 244 Hz–4.6 kHz |
| 7 | Volume | Level | Level | Level (Volume pot) | Level |
| 8 | Output | Output | Output | Output | Output |

All eight sit on **one page** (Move has eight knobs, so there is no page switching).
From v0.6 the layout is **top row (1–4) = Mix and tone, bottom row (5–8) = levels**
(Input, Level/Volume, Output), with Mix always on knob 1 and Output on knob 8 (the
MT-2 has more tone knobs, so Mid/Mid Freq go in the bottom row too). Only Rat Drive
has a **Model** (RAT / RAT2) switch on a second page. In the second page's position
is **Presets** (a browser of factory presets), followed by Schwung's standard My
Presets / Module pages.

### Presets (five each, all level-matched)

| Rat Drive | Green Screamer | Orange Distortion | Blues Drive | Metal Sweep |
|---|---|---|---|---|
| Classic Rat | Classic 808 | Classic DS | Classic Blues | Classic Metal |
| Clean Boost (+4 dB) | Clean Boost (+4 dB) | Bright | Clean Crunch | Scooped (big mid cut) |
| Heavy (tightened with Low Cut) | Mid Push (Bass to the left, thinner still) | Dark | Edge | Mid Grind (1.1 kHz lifted) |
| Synth Grind (Mix 70 %) | Bass Friendly (Bass to the right + Mix 80 %) | Heavy | Full Gain | Industrial (2.3 kHz, Mix 80 %) |
| Bass Rat (Mix 50 %, lows stay clean) | Synth Drive | Industrial | Synth Crunch | Bass Destroy (Mix 60 %) |

Except for Clean Boost, all are within ±1.1 dB of the input on a plucked phrase
(`build/tests/analyze <so> x presets`).

## Circuit models

In every pedal, the clipper is **solved every sample as the circuit's node
equation** (not a table lookup). `G·v + Id(v) = J` (Id = the diode pair, with the
capacitors folded into G and J as trapezoidal companions) is solved with the
closed-form Wright omega solution for the dominant diode (the approximation of
D'Angelo et al., DAFx 2019). The error is under 2 mV, and the harmonics agree within
0.6 dB with a 16×, double-precision, Newton-converged reference solution
(`tests/test_common.c`). The linear parts are written as transfer functions H(s)
from the component values and bilinear-transformed.

**Rat Drive** (v0.3) — the op-amp stage is ported from dm-Rat (Dave Mollen,
GPL-3.0) by way of schwung-303: a third-order op-amp filter whose coefficients change
with the Distortion pot (s-domain coefficients → bilinear transform) and a GBW
correction. It is confirmed to match the coefficient vectors of dm-Rat's own unit
tests (`tests/test_rat_port.c`). Added: slew-rate (0.3 V/µs) and output-swing limits.
The clipper is the circuit itself: C7 4.7µ → R6 1k → node D (the 1N914 pair and the
Filter network = 1.5k + 100k pot → 3.3nF, with a 1M JFET buffer load) are solved as
one circuit every sample, so, as on the original, **brightening the Filter changes
how it distorts** (**darker clockwise**: about 19 kHz → 420 Hz). 4× oversampling.

**The Model switch on the second page** chooses between **RAT = LM308** (30 pF
compensation, the early type) and **RAT2 = OP07DP** (the RAT2 from around 1996 on).
The circuit is the same; only the op-amp difference is modelled: GBW (the OP07 is
about 1.5×, so highs are +3 to 4 dB at high gain) and output impedance (the LM308
drives the diodes softly at about 400 Ω, the OP07 hard at about 60 Ω). Both use
values based on datasheets and measurements. At low gain the two are the same.
Switching moves smoothly over 20 ms. In v0.4, C7 (4.7 µF) is also solved together
with the diodes and Filter instead of being a fixed high-pass, and the estimated
"slow recovery from saturation" added in v0.3 was removed, as no measurement backed
it and its effect could not be measured. In v0.5 RAT alone got an input reference
12 dB lower (Move's -6 dBFS = about 60 mV, an ordinary guitar). The RAT has about
44 dB of gain at noon and distorts from about 4 mV, so with the shared reference it
was always fully saturated at Move's levels and did not change when played harder
(at Distortion 50 %, +14 dB of input gave +0.1 dB of output). The circuit is unchanged.

**Green Screamer** — the circuit is solved as it is: the current passed by the
non-inverting amplifier's gain leg R4 4.7k + C3 47n flows through the feedback
network (R6 51k + Drive 500k A ‖ C4 51p ‖ the diode pair). Output = input + feedback
voltage, so below 720 Hz the gain is small (bass cut), the clean signal is always
underneath, and the distorted part never exceeds the diode voltage (which is why it is
an overdrive). The diodes get a 20 % mismatch, reproducing the original's faint even
harmonics. The transfer function of the tone stage (1k/0.22µ, 10k, 20k pot,
220Ω+0.22µ, 1k feedback) was derived here by nodal analysis. Bass is an extension not
on the original: 50 % = the original's 720 Hz; to the left it rises to 1.8 kHz and also
cuts with a low shelf; to the right it goes down to 90 Hz (the classic "C3 bass mod").

**Orange Distortion** — input buffer → transistor booster (v0.3: as the circuit has
it with C3 47n and shunt feedback R7 470k ‖ C4 250p, **a high-pass cutting below about
310 Hz plus about 33 dB**, inverting, with asymmetric saturation soft on the cutoff
side and hard on the saturation side; up to v0.2 it was a flat 35 dB from 33 Hz, which
saturated the lows too and gave a loose sound) → 23 Hz coupling HPF → op-amp stage
G = 1+(Dist 100k‖100p)/(4.7k+0.47µ) → swing limit → **R14 2.2k → 1N4148 pair ‖ C10
10n** (hard clipping + a 7.2 kHz RC, solved as a node) → passive tone (6.8k/0.1µ LPF,
22n/6.8k HPF, 20k pot, 500k load). In v0.2 **the diode node and the tone circuit are
solved together as one circuit every sample**, so the tone circuit's load varies with
frequency and the Tone position (v0.1 used a fixed 3.4 kΩ, and the level with weak
input and the onset of distortion were off). At noon there is a scoop of about 9.5 dB
around 700 Hz, and Tone moves 5 kHz by 24 dB. The DS-1 is an inverting circuit (the
booster is common-emitter), so the polarity is restored at the output so it does not
cancel against the dry signal in Mix.

**Blues Drive** (v0.2) — rebuilt following the **genuine schematic and parts list**
of the BD-2 published by Aion FX (only the component values were used). It is
**two discrete op-amp stages with no clean path** (a JFET differential pair + PNP),
raised together by a ganged dual 250k A pot: stage 1 is 1+(R8 22k+GAIN)‖C4 47p /(R7
1k5+C3 150n), stage 2 1+(R19 33k+GAIN)‖C10 100p /(R18 2k2+C11 1µ). Each stage clips
asymmetrically at its own output swing. Between the stages is a **Fender-type tone
stack** fixed at T0/M6/B10 (C6 220p + R11 330k, R12 100k, C7 100n, C8 47n + R14 15k,
R13 1M), with a pair of two series 1N914s (D2–D5) at its output. After stage 2 come a
high shelf (R22 5k6 ‖ C13 6n8, C14 5n6) and a **passive treble cut** (C15 18n, 10kB
pot, C16 18n: flat above 1 kHz at noon), and there is **another diode pair (D6/D7)
after the Volume pot**, so raising Level clips at the output too. Last is a 120 Hz bump
(+6 dB) from a 31.6 H gyrator. Every passive circuit is solved as a circuit every
sample, so the load the diodes see is the stack itself. The whole is non-inverting
(v0.1 inverted stage 2, which cancelled against the dry signal in Mix). The maximum
end-to-end gain is about 51 dB (PedalPCB measured 52 dB). At Gain 25 % it is nearly
transparent; above 75 % it breaks up heavily.

**Metal Sweep** (v0.2) — the structure of Electric Druid's analysis: input filter
(106 Hz HPF) → **pre-emphasis stage** (a +35 dB mid hump from a 909 Hz, Q 2.1 gyrator,
rolling off above 7.2 kHz, itself clipping at the rails) → a 10k/10k divider LPF
(262 Hz) → 48 Hz HPF → **high-gain stage ×2 to ×252** (13 kHz with 47p) → rails →
2.2k + silicon diode pair → a soft post-clip LPF → **fixed post-EQ** (twin peaks at
101 Hz +9.4 dB and 4.6 kHz +11 dB = a mid scoop) → **active EQ**: Low (105 Hz,
±19 dB), High (2.4 kHz shelf, ±20 dB), **sweepable Mid ±15.7 dB, 244 Hz to 4.6 kHz
(up to 6.1 kHz when cutting: the Wien bridge's asymmetry)**. Low/High/Mid follow the
original's **cut/boost circuit pot curve** (almost no change around noon, ±7 dB at
10 %/90 %, and a sudden ±19 dB at the ends). In v0.2 the parts the analysis article
gives no numbers for (the pre-stage gyrator's Q, the post-stage peaks the transistors
can actually deliver, the EQ bandwidths, the pot curves) were set by fitting to **the
frequency response measured as a black box** of brummer10's MetalTone, generated from
the MT-2 schematic (matching within 1.3 dB rms for the pre-stage and 0.2 dB rms for the
post-stage). None of MetalTone's code or coefficients are used (its licence is
unclear, so it was only measured).

## Measurements (details in `docs/MEASUREMENTS.md`)

For each pedal at Drive/Gain = 0.25 / 0.5 / 0.75 / 1.0, WAVs
(`build/analysis/<pedal>/drive_XXX.wav`: a plucked phrase, 110 Hz, 1 kHz, a sweep),
THD, H2/H3, the even/odd ratio and the amount of aliasing are produced. Oversampling
was measured at 1x/2x/4x for every pedal, and 4x was chosen only where it clearly
improved things for a small CPU increase (RAT: about 20 dB better; DS-1: -39 → -60 dB
at default settings). The MT-2 was originally specified at 2x, but at maximum
Distortion non-harmonics of -29 dB (1 kHz) remained, so v0.3 made it 4x (-35 dB; at
5 kHz -32 → -70 dB).

## Tests (`bash scripts/test.sh`)

| Test | What |
|---|---|
| `test_common.c` | Half-band (±0.0001 dB to 20 kHz, images/aliases < -90 dB), the diode solution, the TS/DS-1 clippers against a 16× exact solution, smoothing, filters |
| `test_rat_port.c` | Coefficients match dm-Rat's own test vectors |
| `test_pedal.c` | The real .so through the v2 ABI: no NaN/Inf/output on silence, impulse decay, 100 Hz/1 kHz/5 kHz, a sweep, white noise, full scale, all 256 corners of the 8 knobs, random automation of every knob, zipper noise, **bit-identical bypass**, **L/R independence**, mono in → mono out, state round trip, presets, invalid input, refusing a 48 kHz host, instance exhaustion. Also run under ASan+UBSan |
| `test_character.py` | Circuit traits: RAT's Filter direction, the TS's mid hump/Tone/Bass, the DS-1's scoop/Tone range/hard clipping, the BD-2's wide band/asymmetry/gain range, the MT-2's Mid ±15 dB/frequency sweep/Low/High |

## Mix phase alignment (every pedal)

When Mix blends in the dry signal, the wet side is delayed by the oversampling
half-band filters and the circuit's filters, so adding them at equal power cancelled
some bands (at Mix 50 % the TS808 lost −17 dB at 4 kHz, the BD-2 −14 dB at 7 kHz). Now,
whenever Mix is above 0, the dry signal is delayed by the 4–5 samples (about 0.1 ms)
measured for each pedal to line it up (`tools/mix_align.sh`). At Mix 0 % nothing is
delayed (the bypass stays bit-identical), and it switches over smoothly in the first
5 % as Mix goes up. The BD-2 and DS-1 also have their polarity aligned. The worst
notch is within −5 dB on every pedal (pinned by `tests/test_character.py`).

## Output handling (v0.5.1 / v0.1.1 / v0.3)

Previously the shared code **cut the wet signal at ±4 (+12 dBFS) before Output**.
Raising Level or Low/Mid on the MT-2 pushed legitimate signal past that, so lowering
Output still left the waveform squashed (at Level 75 %, Low/Mid maximum, 150 Hz, about
65 % of samples were stuck at the limit). Now only a broken state, such as NaN/Inf or
something beyond +80 dBFS, is detected and resets the state, and the only real cut is
the int16 conversion after Output. `test_output_headroom` in `tests/test_pedal.c` pins
this in a form that reliably catches the old implementation.

## Realtime safety

`process_block`, `set_param`, `get_param` and `create_instance` are called on Move's
SPI callback, so: no memory allocation (instances too come from a static pool with an
atomic CAS), no locks, no file I/O, no logging, no exceptions. Denormal protection sets
the FZ bit **only for the duration of the block** and restores it (so Move's thread
settings are not polluted), plus flushing of state variables. When NaN/Inf or more than
+80 dBFS is detected the state is reset and it recovers on the spot (the only waveform
clipping is the int16 conversion after Output), and DC is removed by an output coupling
HPF. Coefficients are recomputed only while a knob moves, once every 16 frames.

## Open questions / to check on the hardware

- **CPU not measured on the hardware** (no SSH and no ARM64 machine at the time). On
  the host, all five at once in the worst case (stereo) take 225 µs per 128 frames
  (114 µs with a mono source). The estimate scaled to an A72 is 900–1580 µs (31–54 % of
  a 2902 µs block). If heavy, make the DS-1 2x (69 → 46 µs); a mono source is nearly
  half. Check in Schwung Manager → System → CPU (`/system/cpu`). If too heavy, the first
  step is lowering `oversampling` in `pedal.json` (DS-1/RAT to 2x).
- Tuning by ear (especially the gain of the MT-2's fixed peaks, the BD-2's inter-stage
  network, and the ranges of each Presence/Bass).
- The Presets page's browser display, Module Help, and how knob names are shown
  (`Presence` → `PRESNC`, `Distortion` → `DST`, `Mid Freq` → `MIDFRQ`).
- The right Input setting for synths and drums (relative to 0.5 V/FS at the circuit input).
- Full bypass at Mix 0 % (CPU 0), and no click the moment Mix goes up.

## Licence

- `src/common/`, `src/ts808/`, `src/ds1/`, `src/bd2/`, `src/mt2/`, tools and tests:
  **MIT** (`LICENSE`).
- `src/rat/`, `tests/test_rat_port.c` and the built `pfx-rat`: **GPL-3.0-only**
  (`LICENSE-GPL-3.0`), because they are ported from dm-Rat (Dave Mollen, GPL-3.0) and
  schwung-303 (Charles Vestal, GPL-3.0).
- The sources ported, the files ported and the changes; the implementations only
  consulted (TS-808-Ultra, guitarix, ToneTwistPlugs, PedalKernel) and how they were
  treated; and the circuit analyses and papers referred to are all written up in
  `THIRD_PARTY_LICENSES.md`. ToneTwistPlugs' MetalTone has no LICENSE in its repository
  and no licence notice in its DSP files, so none of its code is used. PedalKernel
  (AGPL-3.0 with additional terms) is not used.
