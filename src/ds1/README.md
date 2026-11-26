# Orange Distortion (pfx-ds1)

DS-1-style distortion for Schwung on Ableton Move.
**Unofficial emulation — not affiliated with, or endorsed by, BOSS / Roland.**
"DS-1" is used only to identify the circuit.

Original implementation from the published circuit (ElectroSmash component
values) and first principles; no third-party code. Transistor booster (v0.3:
shunt feedback through C3 47n makes it a ~310 Hz high-pass with ~33 dB above
it, as in the circuit -- v0.1-0.2 treated it as flat 35 dB from 33 Hz, which
sent the whole low end into the clippers; its own asymmetric saturation), op-amp gain stage (Distortion 100k || 100 pF
over 4k7 + 0.47 uF), the 2k2 / 10 nF / 1N4148-pair hard clipper solved as a
circuit node, and the passive tone network derived by nodal analysis (the
~500 Hz scoop at noon). 4x oversampled.

v0.2: the clipper node and the tone network are solved as ONE circuit per
sample, so the network loads the diodes with its real frequency- and
Tone-dependent impedance (v0.1: a fixed 3k4). The DS-1 inverts (its booster is
a common-emitter stage); the output is flipped back so Mix blends in phase.

| Knob (top row: tone, bottom row: levels) | | |
|---|---|---|
| 1 Mix | dry / wet | extension |
| 2 Distortion | op-amp gain x1 to x22 | pedal |
| 3 Tone | 0 dark, 50 % scooped, 100 % bright | pedal |
| 4 Bass | +-10 dB shelf at 120 Hz | extension |
| 5 Presence | +-9 dB shelf at 3.5 kHz | extension |
| 6 Input | +-24 dB into the pedal | extension |
| 7 Level | output level | pedal |
| 8 Output | +-18 dB | extension |

Presets: Classic DS, Bright, Dark, Heavy, Industrial.

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
