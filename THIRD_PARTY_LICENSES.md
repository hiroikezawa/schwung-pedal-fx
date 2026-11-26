# Third-party code, references and licences

Licences were checked **before** any code was written (2026-09-28). The rule
applied: code is ported only from sources whose licence is clear and
compatible; everything else is used, at most, as a reference for circuit
facts, and no code, coefficients or tables are taken from it.

## 1. Code ported into this project

### dm-Rat — Dave Mollen — GPL-3.0

- Repository: https://github.com/davemollen/dm-Rat (commit `fa23188`, 2026-03-23)
- Licence: GNU GPL v3 (repository `LICENSE`, full text in `LICENSE-GPL-3.0`)
- Ported into `src/rat/rat.c` (via schwung-303, below):

  | dm-Rat file | what was taken |
  |---|---|
  | `rat/src/op_amp.rs` | R1, C1, Z1 constants; `get_s_domain_coefficients` |
  | `rat/src/op_amp/bilinear_transform.rs` | third-order bilinear transform |
  | `rat/src/op_amp/third_order_iir_filter.rs` | third-order TDF-II filter |
  | `rat/src/op_amp/op_amp_correction.rs` | gain-bandwidth correction (1.119 MHz / gain) |
  | `rat/src/clipper.rs` | v0.1 only: the curve `x / (1 + x^4)^(1/4)`; **replaced in v0.2** by a circuit solve of the diode + Filter node |
  | `rat/src/tone.rs` | tone RC values and cutoff formula |
  | `rat/src/params.rs` | cubic Distortion / Filter tapers |
  | `rat/src/shared/one_pole_filter.rs` | one-pole low-pass form |
  | `rat/src/op_amp.rs` (tests module) | expected coefficient vectors, in `tests/test_rat_port.c` |

- Changes: C instead of Rust; the 8x FIR oversampler replaced by a 4x
  polyphase-IIR half-band around the nonlinear part only; LM308 slew-rate
  limit (0.3 V/us) and output-swing limit added before the clipper; Volume
  uses this project's dB taper instead of the cubic one; parameter smoothing
  at a 16-frame control rate instead of per-sample linear ramps; Low Cut,
  Hi Cut, Mix, Input and Output added; stereo; DC blocker.

### schwung-303 — Charles Vestal and contributors — GPL-3.0

- Repository: https://github.com/charlesvestal/schwung-303 (commit `ccc2f1f`, 2026-08-31)
- Licence: GNU GPL v3 (its `LICENSE`, which states version 3)
- Ported: `src/dsp/drive.h`, namespace `drive::rat` (Copyright (C) 2026
  Charles Vestal; itself a port of dm-Rat) — the C++ structure that
  `src/rat/rat.c` follows: `ThirdOrderIIR`, `BilinearT`, `OnePoleLP`,
  `update_op_amp_coeffs`, the clip function and the per-block coefficient
  update. The 303-specific parts (Soft model, dispatcher, fixed mid tone,
  19 kHz biquad oversampling) were not taken.
- Changes: as for dm-Rat above; separated from the synth into a standalone
  Audio FX with the Filter exposed as a knob (schwung-303 fixed it at noon).

**Consequence:** `src/rat/`, `tests/test_rat_port.c` and the built
`pfx-rat.so` / `pfx-rat-module.tar.gz` are **GPL-3.0-only**. The module ships
`LICENSE` = the GPL text.

### Schwung — Charles Vestal — MIT

- https://github.com/charlesvestal/schwung, `LICENSE`: MIT, Copyright (c)
  2025-2026 Charles Vestal.
- Used: the plugin API headers `src/host/audio_fx_api_v2.h` and
  `src/host/plugin_api_v1.h` are compiled against (from a sibling checkout,
  `SCHWUNG_ROOT`); not copied into this repository.

### HIIR design procedure — Laurent de Soras — WTFPL

- http://ldesoras.free.fr/prod.html#src_hiir
- `tools/design_halfband.py` and the allpass-chain processing in
  `src/common/oversampling.h` re-implement the published elliptic polyphase
  half-band design and the two-path up/down-sampler structure that HIIR
  implements (after Valenzuela & Constantinides, 1983). WTFPL places no
  conditions; credited for honesty.

## 2. Consulted for comparison only — no code, coefficients or tables used

| Source | Licence | Status |
|---|---|---|
| TS-808-Ultra, James Stubbs (github.com/JamesStubbsEng/TS-808-Ultra, `1a9dfe3`) | GPL-3.0 (clear) | Read to compare approaches (WDF clipper, Yeh tone TF). **Not ported**: its WDF needs chowdsp + JUCE and costs more than a closed-form node solve; our tone TF was derived independently by nodal analysis, and its `X` term does not reduce to our derivation, so its code was not taken even as a check. |
| guitarix `ts9sim.dsp`, `bossds1.dsp` (github.com/brummer10/guitarix) | GPL-2.0-or-later (repo `COPYING`: whole = GPL-3.0-or-later) | Read to compare. `ts9sim` is a first-order drive filter + table + one-pole tone: too simple for the brief. `bossds1` coefficients are circuit-generated; **none were used** — our DS-1 filters are derived from component values. |
| ToneTwistPlugs `MetalTone`, brummer10 (github.com/brummer10/ToneTwistPlugs, `5fac290`) | **Unclear**: no repository LICENSE file; the Faust-generated DSP files (`MetalTone_pre/dist/post.cc`, `metalzone_p2_table.h`, `metaltone_post.dsp`) carry no licence header; only the DPF wrapper files say "GPL-2.0" / "GPL-2.0-or-later" | **Not copied**, per the rule. Its externally visible structure was used as a cross-check, and (v0.2) its linear pre and post stages were compiled privately, outside this repository, and **measured as a black box**: the frequency responses (which describe the BOSS circuit it was generated from) were used to fit Metal Sweep's own parameters (pre-gyrator 909 Hz / Q 2.1, post peaks 101 Hz / 4.6 kHz, EQ bandwidths and pot laws). No MetalTone code, coefficient or table is in this project. |
| PedalKernel (Blues Driver model) | AGPL-3.0 + additional terms | **Not used and not read for code**, as instructed. |

## 3. Published circuit analyses and papers (facts, not code)

- ElectroSmash, "Tube Screamer Analysis" — TS808 component values, gain,
  720 Hz / 723 Hz corners, tone circuit (mirror: electrosmash.mas-effects.com).
- ElectroSmash, "Boss DS-1 Distortion Analysis" — every DS-1 component value,
  booster gain 35 dB, HPF corners, R14/C10 7.2 kHz, tone network corners.
- Electric Druid, "Boss MT-2 Metal Zone pedal analysis" — MT-2 stage
  structure, 106 Hz / 48 Hz / 340 Hz / 7.2 kHz / 13 kHz corners, 36 dB
  pre-stage peak, gain x2..x252, gyrator peaks 105 Hz (Q 14.6) and 4898 Hz
  (Q 2.2), Low 105 Hz (Q 3.1), Wien-bridge mid 240 Hz-4.7 kHz boost /
  6.3 kHz cut, +-15 dB.
- Aion FX, "Sapphire Amp Overdrive" project documentation v1.0.1 (2024):
  the complete stock BD-2 schematic and parts list, which Blues Drive v0.2 is
  modelled from (component values only; no code exists there to copy).
- Analog Is Not Dead, "Circuit Analysis: the Boss BD2"; PedalPCB forum,
  "This Week on the Breadboard: Blues Driver BD-2" — BD-2 stage formulas
  (1 + (22k+VR)/1k5, 1 + (33k+VR)/2k2), 700 Hz / 100 Hz corners, 52 dB
  end-to-end, inter-stage network, gyrator bump.
- D. T. Yeh, J. S. Abel, J. O. Smith, "Simplified, physically-informed
  models of distortion and overdrive guitar effects pedals", DAFx-07 — the
  approach of solving each pedal stage as a circuit.
- S. D'Angelo, L. Gabrielli, L. Turchet, "Fast approximation of the Lambert W
  function for virtual analog modelling", DAFx 2019 — the Wright omega
  approximation (`pfx_omega3/4`).
- A. Valenzuela, A. G. Constantinides, "Digital signal processing schemes
  for efficient interpolation and decimation", IEE Proc. G, 1983.
- Standard SPICE card for 1N4148 (Is = 2.52 nA, n = 1.752).
- BOSS published specifications of the MT-2 EQ ranges.

Every model built from these is original code under the MIT licence
(`src/common/`, `src/ts808/`, `src/ds1/`, `src/bd2/`, `src/mt2/`).
