# Metal Sweep (pfx-mt2)

MT-2-style metal distortion for Schwung on Ableton Move.
**Unofficial emulation — not affiliated with, or endorsed by, BOSS / Roland.**
"MT-2" / "Metal Zone" are used only to identify the circuit.

Original implementation from the published circuit analysis (Electric Druid)
and BOSS's published EQ ranges; no third-party code. Input filter -> 36 dB
mid-hump pre-emphasis stage (clips on its rails) -> 340 Hz divider -> high-gain
stage x2..x252 -> rails -> silicon diode clipper -> post-clip shelf -> the fixed
double-peaked post EQ (101 Hz, 4.6 kHz) -> active Low / High and the
sweepable parametric Mid. 4x oversampled (v0.3; 2x left -29 dB of
aliasing at full Distortion).

v0.2: the shaping constants Electric Druid does not publish (the pre-stage
gyrator's Q, the post peaks the transistors can really deliver, the EQ
bandwidths and the pot laws) were fitted to frequency responses measured
from brummer10's schematic-derived MetalTone, run as a black box. No
MetalTone code or coefficients are used.

| Knob (top row: tone, bottom row: levels) | | |
|---|---|---|
| 1 Mix | dry / wet | extension |
| 2 Distortion | x2 to x252 | pedal |
| 3 Low | +-19 dB bell at 105 Hz, real cut/boost pot law | pedal |
| 4 High | +-20 dB shelf at 2.4 kHz, real pot law | pedal |
| 5 Mid | +-15.7 dB, Q 0.66 | pedal |
| 6 Mid Freq | 244 Hz - 4.6 kHz (cut reaches 6.1 kHz), log | pedal |
| 7 Level | output level | pedal |
| 8 Output | +-18 dB | extension |

Presets: Classic Metal, Scooped, Mid Grind, Industrial, Bass Destroy.

Licence: MIT.

## Conventions (all five pedals)

- Every knob is 0..1 on the wire (shown as 0-100 %). **50 % Level / Volume =
  the input's loudness** at default settings (calibrated on a -6 dBFS-peak
  plucked-string phrase), +-20 dB either side, off at 0 %.
- Input (where present): +-24 dB into the pedal, 50 % = 0 dB.
  Output: +-18 dB after the Mix, 50 % = 0 dB.
- Mix is an equal-power dry/wet crossfade. **Mix 0 % with Output 50 % is a
  bit-exact bypass that costs no DSP.**
- Digital full scale = 0.5 V at the modelled pedal input, so a Move track
  peaking at -6 dBFS hits the circuit like a hot humbucker.
- Stereo: two identical, independent channels; mono in gives mono out, and a
  mono signal is processed once (half the CPU).
- Parameters are smoothed (Drive/Gain 5 ms, tone 10 ms, levels 15 ms).
