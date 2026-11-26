# Blues Drive (pfx-bd2)

BD-2-style overdrive for Schwung on Ableton Move.
**Unofficial emulation — not affiliated with, or endorsed by, BOSS / Roland.**
"BD-2" is used only to identify the circuit.

v0.2 is modelled from the full stock BD-2 schematic and parts list published
by Aion FX (Sapphire Amp Overdrive documentation) -- facts only, no code. Two
discrete op-amp stages (JFET pair + PNP) on one ganged 250kA Gain pot, each
with its own frequency-dependent feedback (R7 1k5 + C3 150n, R18 2k2 + C11
1 uF; C4 47p and C10 100p across the feedback) and its own asymmetric output
swing; between them a Fender-type tone stack frozen at T0/M6/B10 (C6 220p +
R11 330k, R12 100k, C7 100n, C8 47n + R14 15k, R13 1M) whose output carries
the D2-D5 diodes; after stage 2 a treble shelf (R22 5k6 || C13 6n8, C14 5n6)
and the PASSIVE Tone (C15 18n, 10kB pot, C16 18n), then the Volume pot into
the D6/D7 diodes, then the 120 Hz gyrator bump. Every passive network is
solved as a circuit per sample, so the stack is exactly what the diodes see.
Non-inverting overall, so Mix blends in phase. 2x oversampled through the
two gain stages.

| Knob (top row: tone, bottom row: levels) | | |
|---|---|---|
| 1 Mix | dry / wet | extension |
| 2 Gain | both stages (dual 250k audio taper) | pedal |
| 3 Tone | passive treble cut: flat above 1 kHz at 50 %, -16 dB at 10 kHz fully left | pedal |
| 4 Bass | +-9 dB shelf at 100 Hz | extension |
| 5 Presence | +-9 dB shelf at 4.5 kHz | extension |
| 6 Input | +-24 dB into the pedal | extension |
| 7 Level | the Volume pot (100kA); high settings also drive the D6/D7 output diodes, as on the pedal | pedal |
| 8 Output | +-18 dB | extension |

Presets: Classic Blues, Clean Crunch, Edge, Full Gain, Synth Crunch.

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
