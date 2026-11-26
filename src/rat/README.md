# Rat Drive (pfx-rat)

RAT-style op-amp distortion for Schwung on Ableton Move.
**Unofficial emulation — not affiliated with, or endorsed by, ProCo Sound /
Radial Engineering.** "RAT" is used only to identify the circuit studied.

The model is a port of Dave Mollen's **dm-Rat** (GPL-3.0) by way of Charles
Vestal's **schwung-303** RAT drive: a distortion-pot-dependent third-order
op-amp filter (bilinear transform of the circuit's s-domain transfer
function) and the op-amp gain-bandwidth correction. Added here: LM308
slew-rate (0.3 V/us) and output-swing limits, 4x polyphase-IIR oversampling
around the nonlinear part, and the extension knobs.

v0.2: the clipper is the circuit, not a curve. C7 / R6 1k feed node D, where
the 1N914 pair AND the Filter network (1k5 + 100k pot into 3n3, loaded by the
JFET buffer) meet; the node is solved per sample. So, as on the pedal, a
bright Filter loads the diodes and a dark one does not.

| Knob (top row: tone, bottom row: levels) | | |
|---|---|---|
| 1 Mix | dry / wet | extension |
| 2 Distortion | op-amp gain, 0 to 67 dB (dm-Rat cubic taper) | pedal |
| 3 Filter | RC low-pass 32 kHz -> 475 Hz, **clockwise = darker** | pedal |
| 4 Low Cut | 12 dB/oct HPF before the drive, 20 Hz -> 1 kHz | extension |
| 5 Hi Cut | 12 dB/oct LPF after, off -> 1 kHz | extension |
| 6 Input | +-24 dB into the pedal | extension |
| 7 Volume | output level | pedal |
| 8 Output | +-18 dB | extension |

Page 2, **Model** (a switch): **RAT** = LM308 with its 30 pF compensation (the
original) / **RAT2** = OP07DP (what ProCo fitted from ~1996, when the LM308 went
obsolete). Same 0.3 V/us slew; the OP07 has ~1.5x the gain-bandwidth (more top
end at high gain) and a lower output impedance (drives the diodes harder than
the LM308's ~400 ohm). At low gain the two are identical. Switching
crossfades over 20 ms.

v0.4: C7 (4.7 uF, in series with R6) is solved together with the diodes and
the Filter, so its charge moves with the diodes' conduction as in the
circuit, instead of being a fixed 34 Hz high-pass. v0.3's estimated LM308
"saturation wind-up" was removed: it had no measurement behind it and no
measurable effect.

v0.5: input reference. The other pedals read digital full scale as 0.5 V at
the circuit input; the RAT reads it 12 dB lower, so a Move track peaking at
-6 dBFS arrives as ~60 mV, an ordinary guitar. The circuit has ~44 dB of
op-amp gain at noon and clips from ~4 mV, so at the 0.5 V reference every
Move-level signal sat deep in saturation and playing harder changed nothing
(+14 dB in gave +0.1 dB out at Distortion 50 %). Now: dynamics survive at
low settings, compression grows with Distortion, as on the pedal.

Presets: Classic Rat, Clean Boost, Heavy, Synth Grind, Bass Rat, RAT2 Classic,
RAT2 Tight.

Licence: **GPL-3.0-only** (derived from dm-Rat and schwung-303); see LICENSE
and THIRD_PARTY_LICENSES.md.

## Conventions (all five pedals)

- Every knob is 0..1 on the wire (shown as 0-100 %). **50 % Level / Volume =
  the input's loudness** at default settings (calibrated on a -6 dBFS-peak
  plucked-string phrase), +-20 dB either side, off at 0 %.
- Input (where present): +-24 dB into the pedal, 50 % = 0 dB.
  Output: +-18 dB after the Mix, 50 % = 0 dB.
- Mix is an equal-power dry/wet crossfade. **Mix 0 % with Output 50 % is a
  bit-exact bypass that costs no DSP.**
- Digital full scale = 0.5 V at the modelled pedal input, so a Move track
  peaking at -6 dBFS hits the circuit like a hot humbucker. (Rat Drive: 12 dB
  lower, an ordinary guitar; see above.)
- Stereo: two identical, independent channels; mono in gives mono out, and a
  mono signal is processed once (half the CPU).
- Parameters are smoothed (Drive/Gain 5 ms, tone 10 ms, levels 15 ms).
