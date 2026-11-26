# Green Screamer (pfx-ts808)

TS808-style overdrive for Schwung on Ableton Move.
**Unofficial emulation — not affiliated with, or endorsed by, Ibanez
(Hoshino Gakki) or Maxon.** "TS808" is used only to identify the circuit.

Original implementation from the published circuit (ElectroSmash component
values) and first principles; no third-party code. The clipping amplifier is
solved as a circuit: the R4/C3 gain leg (the 720 Hz bass cut that makes the
mid hump) drives the feedback network (R6 + Drive || 51 pF || diode pair),
whose node equation is solved every sample with a closed-form diode solution
and a trapezoidal capacitor model, 2x oversampled. The tone amplifier's
transfer function is derived by nodal analysis of the tone circuit.

| Knob (top row: tone, bottom row: levels) | | |
|---|---|---|
| 1 Mix | dry / wet | extension |
| 2 Drive | 51k-551k feedback, audio taper | pedal |
| 3 Tone | clockwise = brighter (S-taper) | pedal |
| 4 Bass | 50 % = stock 720 Hz corner; left thinner, right down to 90 Hz (the C3 bass mod) | extension |
| 5 Presence | +-9 dB shelf at 3.5 kHz | extension |
| 6 Input | +-24 dB into the pedal | extension |
| 7 Level | output level | pedal |
| 8 Output | +-18 dB | extension |

Presets: Classic 808, Clean Boost, Mid Push, Bass Friendly, Synth Drive.

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
