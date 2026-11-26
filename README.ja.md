# schwung-pedal-fx

[English](README.md) | **日本語**

Ableton Move上のSchwung用 Audio FX として、ギターペダル5種の回路を**回路の式からモデル化**したものです。
5つとも通常のSchwung Audio FX（Audio FX API v2、`component_type: audio_fx`、`chainable`）で、
**Schwung → Signal Chain → Audio FX** から追加できます。1FX=1モジュール（Schwungのchain hostは
`modules/audio_fx/<id>/<id>.so` を読むため）で、共通DSPコードを共有しています。

> **非公式エミュレーションです。** BOSS / Roland、Ibanez（星野楽器）、Maxon、ProCo Sound / Radial
> Engineering、Ableton とは一切関係がなく、承認も受けていません。RAT、TS808、Tube Screamer、DS-1、
> BD-2、Blues Driver、MT-2、Metal Zone は各社の商標で、研究対象の回路を示すためだけに使っています。
> 公開名にはブランド名を使っていません。

| Moveでの表示名 | id | 対象回路 | 実装 | ライセンス | OS | CPU (host, stereo) |
|---|---|---|---|---|---|---|
| **Rat Drive** | `pfx-rat` | RAT | dm-Rat → schwung-303 からの**移植** + 拡張 | **GPL-3.0-only** | 4x | 39.4 µs/block |
| **Green Screamer** | `pfx-ts808` | TS808 | 回路図・論文から**新規実装** | MIT | 2x | 26.2 µs |
| **Orange Distortion** | `pfx-ds1` | DS-1 | 回路図から**新規実装** | MIT | 4x | 69.0 µs |
| **Blues Drive** | `pfx-bd2` | BD-2 | 回路解析から**新規実装** | MIT | 2x | 58.2 µs |
| **Metal Sweep** | `pfx-mt2` | MT-2 | 回路解析から**新規実装** | MIT | 4x | 32.2 µs |

「Metal Zone-style」ではなく **Metal Sweep** にしたのは、"Metal Zone" がBOSSの製品名そのものだからです
（名前は `src/mt2/pedal.json` の `name` 1行で変えられます）。

## 構成

```
schwung-pedal-fx/
  src/common/        共通DSP（MIT）
    filters.h          1次/biquad、s領域→z領域（双一次変換+プリワープ）、シェルフ/ピーク
    oversampling.h     2x/4x ポリフェーズIIRハーフバンド（99 dB / 100 dB）
    smoothing.h        コントロールレート平滑化（16フレーム）+ 音声レートのランプ
    nonlinear.h        ダイオード対の閉形式解（Wright omega）、コンデンサのコンパニオンモデル、レールリミッタ
    tapers.h           A/C/Sカーブ、log周波数、dBノブ、Levelテーパー
    plugin_v2.h        Schwung Audio FX v2アダプタ（Input/Mix/Output、バイパス、NaN対策、プリセット、state）
  src/<pedal>/       rat ts808 ds1 bd2 mt2
    <pedal>.c          回路モデル（1翻訳単位、最後にplugin_v2.hをinclude）
    pedal.json         ノブ・既定値・プリセットの唯一の定義元 → module.jsonとCヘッダを生成
    help.json          Move上のヘルプ（Module Help）
    README.md          モジュールに同梱する英語README（リポジトリ直下の README.md は英語、README.ja.md は日本語）
  scripts/           build.sh  test.sh  install.sh / install.py  gen_meta.py  check_abi.py
  tests/             test_common.c  test_rat_port.c  test_pedal.c  test_character.py
  tools/             analyze.c（WAV・THD・スペクトル・周波数特性・CPU）  os_sweep.sh  design_halfband.py
  docs/MEASUREMENTS.md   計測結果
  LICENSE (MIT + RATの例外)  LICENSE-GPL-3.0  NOTICE  THIRD_PARTY_LICENSES.md
```

## ビルド・テスト・インストール

WSL/Linuxで `gcc`、`aarch64-linux-gnu-gcc`、Python 3（標準ライブラリのみ）が必要です。
Schwungのソースが隣（`../schwung`）にある前提です（`SCHWUNG_ROOT`で変更可）。

```bash
bash scripts/test.sh              # 全テスト（ASan/UBSan含む）→ ARM64ビルド → ABI検査
bash scripts/build.sh arm64       # dist/<id>-module.tar.gz を5つ
bash scripts/build.sh arm64 ds1   # 1つだけ
bash scripts/install.sh http://move.local:7700 --check   # 接続とパッケージ検査のみ
bash scripts/install.sh http://move.local:7700           # 5つ全部をMoveへ
bash scripts/install.sh http://move.local:7700 rat mt2   # 指定したものだけ
```

インストールはSchwung Managerの「Custom install」（`POST /modules/install-custom`）を使うので
SSHは不要です。`modules/audio_fx/<id>/` に入ります。ARM64でない`.so`はアップロードを拒否します。
ARM64の`.so`は glibc 2.29 以下のシンボルだけを使い（Moveは2.34）、公開シンボルは
`move_audio_fx_init_v2` だけです（`scripts/check_abi.py`）。

## ノブ（全ペダル共通の約束）

- ノブ値はすべて **0.0〜1.0**（Move上は0〜100%表示）で、DSP内部で実値に変換します。
- **Level / Volume の50% = 入力と同じ音量**（既定設定で、-6 dBFSピークの撥弦フレーズで校正）。±20 dB、0%で無音。dBスケール（理想化したAカーブ）。
- **Input**（あるもの）：ペダルへの入力 ±24 dB、50% = 0 dB。**Output**：Mix後 ±18 dB、50% = 0 dB。
- **Mix**：等パワーのクロスフェード。**Mix 0% + Output 50% はビット完全一致のバイパスで、DSPを一切実行しません。**
- Drive/Gain：Aカーブ（対数）、Frequency：対数、Tone：回路に合わせたカーブ（TSはS/Gカーブ等）。
- 平滑化：Drive/Gain 5 ms（反応を鈍らせない）、Tone/EQ 10 ms、Mid Freq 20 ms、レベル類 15 ms。
- 入力の電圧基準：デジタルフルスケール = 回路入力で0.5 V（Rat Driveのみ12 dB低い換算）。Moveのトラックが-6 dBFSピークなら、ホットなハムバッカー相当の250 mVで回路に入ります。シンセやドラムで強すぎれば Input を下げてください。

| # | Rat Drive | Green Screamer | Orange Distortion | Blues Drive | Metal Sweep |
|---|---|---|---|---|---|
| 1 | Mix | Mix | Mix | Mix | Mix |
| 2 | Distortion | Drive | Distortion | Gain | Distortion |
| 3 | Filter（右回しで暗く） | Tone | Tone | Tone（受動トレブルカット） | Low ±19 dB |
| 4 | Low Cut 20 Hz–1 kHz | Bass（50%=実機） | Bass ±10 dB | Bass ±9 dB | High ±20 dB |
| 5 | Hi Cut off–1 kHz | Presence ±9 dB | Presence ±9 dB | Presence ±9 dB | Mid ±15 dB |
| 6 | Input | Input | Input | Input | Mid Freq 244 Hz–4.6 kHz |
| 7 | Volume | Level | Level | Level（Volumeポット） | Level |
| 8 | Output | Output | Output | Output | Output |

8つとも**1ページ**に並びます（Moveのノブは8つなのでページ切替なし）。v0.6から、画面の**上段（1〜4）＝Mixと音色、下段（5〜8）＝音量系**（Input・Level/Volume・Output）の配置で、Mixはノブ1、Outputはノブ8に統一しました（MT-2は音色ノブが多いので下段にMid/Mid Freqも入ります）。Rat Driveだけは2ページ目に **Model**（RAT / RAT2）スイッチがあります。2ページ目の位置に
**Presets**（ファクトリープリセットのブラウザ）があり、Schwung標準の My Presets / Module ページが続きます。

### プリセット（各5つ、すべて音量をそろえてあります）

| Rat Drive | Green Screamer | Orange Distortion | Blues Drive | Metal Sweep |
|---|---|---|---|---|
| Classic Rat | Classic 808 | Classic DS | Classic Blues | Classic Metal |
| Clean Boost（+4 dB） | Clean Boost（+4 dB） | Bright | Clean Crunch | Scooped（中域を大きくカット） |
| Heavy（Low Cutで締める） | Mid Push（Bass左でさらに細く） | Dark | Edge | Mid Grind（1.1 kHz持ち上げ） |
| Synth Grind（Mix 70%） | Bass Friendly（Bass右＋Mix 80%） | Heavy | Full Gain | Industrial（2.3 kHz、Mix 80%） |
| Bass Rat（Mix 50%で低域はクリーン） | Synth Drive | Industrial | Synth Crunch | Bass Destroy（Mix 60%） |

Clean Boost以外は、撥弦フレーズで入力に対して ±1.1 dB 以内に収めています（`build/tests/analyze <so> x presets`）。

## 回路モデル

どのペダルも、クリッパは**回路のノード方程式として毎サンプル解いています**（テーブル参照ではありません）。
`G·v + Id(v) = J`（Id = ダイオード対、コンデンサは台形則コンパニオンでG・Jに畳み込む）を、
優勢なダイオードのWright omega閉形式解（D'Angeloら DAFx 2019の近似）で解きます。誤差は2 mV以下で、
16倍・倍精度・Newton収束の参照解と高調波が0.6 dB以内で一致します（`tests/test_common.c`）。
線形部分は部品値から伝達関数H(s)を書き、双一次変換しています。

**Rat Drive**（v0.3） — オペアンプ段はdm-Rat（Dave Mollen、GPL-3.0）をschwung-303経由で移植：Distortionポットで
係数が変わる3次オペアンプフィルタ（s領域係数→双一次変換）とGBW補正。dm-Rat自身のユニットテストの係数ベクトルと
一致することを確認済み（`tests/test_rat_port.c`）。追加：スルーレート（0.3 V/µs）と出力振幅の制限。
クリッパは回路そのもの：C7 4.7µ → R6 1k → ノードD（1N914対とFilter網＝1.5k＋100kポット→3.3nF、JFETバッファ1M負荷）
を1つの回路として毎サンプル解くので、実機と同じく**Filterを明るくすると歪み方が変わります**（**右回しで暗く**：
約19 kHz→420 Hz）。4倍オーバーサンプリング。

**2ページ目の Model スイッチ**で、**RAT＝LM308**（30pF補償、初期型）と**RAT2＝OP07DP**（1996年頃以降のRAT2）を
切り替えます。回路は同じで、オペアンプの違いだけをモデル化しています：GBW（OP07は約1.5倍＝高ゲインで高域が
+3〜4 dB）と出力インピーダンス（LM308は約400 Ωでダイオードを柔らかく、OP07は約60 Ωで硬く駆動）。
どちらもデータシートと実測値に基づく値です。低ゲインでは両者同じ。切り替えは20 msでなめらかに移行します。
v0.4でC7（4.7µF）も固定のハイパスではなくダイオード・Filterと一緒に解くようにし、v0.3で入れていた
推定値の「飽和からの戻りの遅さ」は根拠となる測定がなく効果も測れなかったため外しました。
v0.5でRATだけ入力の換算を12 dB下げました（Moveの−6 dBFS＝約60 mVの普通のギター）。RATは正午で約44 dBの
ゲインがあり約4 mVから歪むため、共通の換算ではMoveの音量だと常に飽和しきって、強く弾いても変化しませんでした
（Distortion 50%で入力+14 dBに対し出力+0.1 dB）。回路はそのままです。

**Green Screamer** — 非反転アンプのゲイン脚R4 4.7k + C3 47nが流す電流が、帰還網（R6 51k+Drive 500k A
‖ C4 51p ‖ ダイオード対）を流れる、という回路そのままを解きます。出力 = 入力 + 帰還電圧なので、720 Hz以下は
ゲインが小さく（bass cut）、クリーン信号が常に下にあり、歪み成分はダイオード電圧を超えません（オーバー
ドライブの理由）。ダイオードは20%のばらつきを持たせ、実機のかすかな偶数次を再現。トーン段（1k/0.22µ、
10k、20kポット、220Ω+0.22µ、1k帰還）の伝達関数は節点解析で自分で導出しました。Bassは実機にない拡張で、
50% = 実機の720 Hz、左で1.8 kHzまで上げてローシェルフでも削り、右で90 Hzまで下げます（定番の「C3 bass mod」）。

**Orange Distortion** — 入力バッファ→トランジスタブースター（v0.3：C3 47nとシャント帰還R7 470k‖C4 250pの回路どおり、
**約310 Hz以下を削るハイパス＋約33 dB**、反転、カットオフ側は柔らかく飽和側は硬い非対称飽和。v0.2までは33 Hzから
フラットな35 dBとしていたため、低域まで飽和して締まりのない音になっていました）→結合HPF 23 Hz→オペアンプ段 G = 1+(Dist 100k‖100p)/(4.7k+0.47µ)→
振幅制限→**R14 2.2k → 1N4148対 ‖ C10 10n**（ハードクリップ+7.2 kHzのRC、ノードとして求解）→パッシブ・トーン
（6.8k/0.1µ LPF、22n/6.8k HPF、20kポット、500k負荷）。v0.2では**ダイオードの点とトーン回路を1つの回路として
毎サンプル解き**、トーン回路の負荷が周波数とTone位置で変わるようにしました（v0.1は固定3.4 kΩで、弱い入力での音量や
歪み始めがずれていました）。正午で約700 Hzに約9.5 dBのスクープ、Toneで5 kHzが24 dB動きます。DS-1は反転回路
（ブースターがエミッタ接地）なので、Mixで原音と打ち消し合わないよう出力で極性を戻しています。

**Blues Drive**（v0.2） — Aion FXが公開しているBD-2の**純正回路図・部品表**どおりに作り直しました（部品値のみ参照）。
**クリーン経路のない2段のディスクリート・オペアンプ**（JFET差動対＋PNP）を連動デュアル250k Aで同時に上げる構造で、
1段目 1+(R8 22k+GAIN)‖C4 47p /(R7 1k5+C3 150n)、2段目 1+(R19 33k+GAIN)‖C10 100p /(R18 2k2+C11 1µ)。各段は自分の
出力振幅で非対称にクリップ。段間はT0/M6/B10に固定した**フェンダー型トーンスタック**（C6 220p＋R11 330k、R12 100k、
C7 100n、C8 47n＋R14 15k、R13 1M）で、その出力点に1N914×2直列の対（D2〜D5）。2段目の後に高域シェルフ
（R22 5k6‖C13 6n8、C14 5n6）と**受動のトレブルカット**（C15 18n、10kBポット、C16 18n：正午で1 kHz以上フラット）、
**Volumeポットの後にもダイオード対（D6/D7）**があり、Levelを上げると出力でもクリップします。最後に31.6 Hのジャイレータ
による120 Hzの山（+6 dB）。受動回路はすべて回路として毎サンプル解くので、ダイオードが見る負荷はスタックそのものです。
全体は非反転（v0.1は2段目を反転させていて、Mixで原音と打ち消し合っていました）。最大の端から端までのゲインは約51 dB
（PedalPCBの実測52 dB）。Gain 25%ではほぼ素通し、75%以上で大きく崩れます。

**Metal Sweep**（v0.2） — Electric Druidの解析の構造：入力フィルタ（106 Hz HPF）→**プリエンファシス段**（909 Hz、
Q 2.1のジャイレータで+35 dBの中域ハンプ、7.2 kHz以上ロールオフ、自身もレールでクリップ）→10k/10k分圧のLPF
（262 Hz）→48 Hz HPF→**高ゲイン段 ×2〜×252**（47pで13 kHz）→レール→2.2k+シリコンダイオード対→ポストクリップの
ソフトLPF→**固定ポストEQ**（101 Hz +9.4 dB、4.6 kHz +11 dBの二峰＝中域スクープ）→**アクティブEQ**：Low
（105 Hz、±19 dB）、High（2.4 kHzシェルフ、±20 dB）、**スイープ式Mid ±15.7 dB、244 Hz〜4.6 kHz（カット時は
6.1 kHzまで：ウィーンブリッジの非対称）**。Low/High/Midは実機の**カット/ブースト回路のポットカーブ**で動きます
（正午付近はほとんど変化せず、10%/90%で±7 dB、端で一気に±19 dB）。
v0.2では、解析記事が数値を出していない部分（プリ段ジャイレータのQ、トランジスタが実際に出せるポスト段ピーク、
EQの帯域幅、ポットカーブ）を、MT-2の回路図から生成されたbrummer10のMetalToneを**ブラックボックスとして測った
周波数特性**にフィットさせて決めました（プリ段 1.3 dB rms、ポスト段 0.2 dB rms で一致）。MetalToneのコードや
係数は一切使っていません（ライセンス不明のため、測定のみ）。

## 計測（詳細は `docs/MEASUREMENTS.md`）

各ペダルについて Drive/Gain = 0.25 / 0.5 / 0.75 / 1.0 で、WAV（`build/analysis/<pedal>/drive_XXX.wav`：
撥弦フレーズ、110 Hz、1 kHz、スイープ）、THD、H2/H3、偶数/奇数比、エイリアス量を出しています。
オーバーサンプリングは1x/2x/4xを全ペダルで測り、明確に改善し、CPU増が小さいものだけ4xにしました
（RAT：約20 dB改善、DS-1：既定設定で-39→-60 dB）。MT-2は当初の指定で2xでしたが、Distortion最大で
非高調波が-29 dB（1 kHz）残っていたため、v0.3で4x（-35 dB、5 kHzでは-32→-70 dB）にしました。

## テスト（`bash scripts/test.sh`）

| テスト | 内容 |
|---|---|
| `test_common.c` | ハーフバンド（20 kHzまで±0.0001 dB、イメージ/エイリアス < -90 dB）、ダイオード解、TS/DS-1クリッパを16倍厳密解と比較、平滑化、フィルタ |
| `test_rat_port.c` | dm-Rat自身のテストベクトルと係数一致 |
| `test_pedal.c` | 実.soをv2 ABIで：無音でNaN/Inf/出力なし、インパルス減衰、100 Hz/1 kHz/5 kHz、スイープ、ホワイトノイズ、フルスケール、8ノブの全256隅、全ノブのランダム自動化、ジッパー、**バイパスのビット一致**、**L/R独立**、モノ入力→モノ出力、state往復、プリセット、不正入力、48 kHzホスト拒否、インスタンス枯渇。ASan+UBSanでも実行 |
| `test_character.py` | 回路の特徴：RATのFilter方向、TSの中域ハンプ/Tone/Bass、DS-1のスクープ/Toneの幅/ハードクリップ、BD-2の広帯域/非対称/ゲイン幅、MT-2のMid ±15 dB/周波数スイープ/Low/High |

## Mixの位相合わせ（全ペダル）

Mixで原音を混ぜるとき、ウェット側はオーバーサンプリングのハーフバンドフィルタと回路のフィルタのぶん遅れるため、
等パワーで足すと打ち消し合う帯域がありました（Mix 50%でTS808は4 kHzが−17 dB、BD-2は7 kHzが−14 dB）。
今はMixが0より上のとき、原音を各ペダルで測った4〜5サンプル（約0.1 ms）遅らせて揃えています（`tools/mix_align.sh`）。
Mix 0%では遅らせない（ビット完全一致のバイパスのまま）で、Mixを上げ始める最初の5%でなめらかに切り替えます。
BD-2とDS-1は極性も揃えました。最悪の谷は全ペダルで−5 dB以内です（`tests/test_character.py`で固定）。

## 出力の扱い（v0.5.1 / v0.1.1 / v0.3）

以前は共通処理がウェット信号を、**Outputより前で±4（+12 dBFS）に切っていました**。MT-2でLevelやLow/Midを上げると
正当な信号がこれを超えるため、Outputを下げても波形は潰れたままでした（Level 75%・Low/Mid最大・150 Hzで約65%の
サンプルが制限に張り付く）。今は、NaN/Infや+80 dBFSを超えるような壊れた状態だけを検出して状態をリセットし、
実際に切るのはOutput調整後のint16変換だけです。`tests/test_pedal.c`の`test_output_headroom`が、旧実装を確実に
検出する形でこれを固定しています。

## リアルタイム安全性

`process_block`・`set_param`・`get_param`・`create_instance` は MoveのSPIコールバック上で呼ばれるので：
メモリ確保なし（インスタンスも静的プールからatomic CASで取得）、ロックなし、ファイルI/Oなし、ログなし、
例外なし。デノーマル対策はFZビットを**このブロックの間だけ**立てて戻す（Moveのスレッドの設定を汚さない）+
状態変数のフラッシュ。NaN/Infや+80 dBFS超を検出したら状態をリセットしてその場で回復（波形のクリップはOutput後のint16変換だけ）、
DCは出力結合HPFで除去。係数の再計算はノブが動いている間だけ、16フレームに1回。

## 未解決・実機で確認すること

- **CPUは実機未計測**（SSHもARM64機もない）。ホストでは5つ同時・最悪条件（ステレオ）で225 µs/128フレーム（モノ音源なら114 µs）。
  A72換算の推定は900〜1580 µs（1ブロック2902 µsの31〜54%）。重い場合はDS-1を2倍に（69→46 µs）、モノ音源ならほぼ半分。
  Schwung Manager → System → CPU（`/system/cpu`）で確認してください。重ければ `pedal.json` の
  `oversampling` を下げる（DS-1/RATを2xへ）のが第一手です。
- 聴感による調整（特にMT-2の固定ピークのゲイン、BD-2の段間ネットワーク、各Presence/Bassの範囲）。
- Presetsページのブラウザ表示、Module Help、ノブ名の表示（`Presence`→`PRESNC`、`Distortion`→`DST`、`Mid Freq`→`MIDFRQ`）。
- シンセ/ドラムでの Input の適正値（回路入力0.5 V/FS基準）。
- Mix 0%での完全バイパス（CPU 0）と、Mixを上げた瞬間のクリックのなさ。

## ライセンス

- `src/common/`、`src/ts808/`、`src/ds1/`、`src/bd2/`、`src/mt2/`、ツール、テスト：**MIT**（`LICENSE`）。
- `src/rat/` と `tests/test_rat_port.c`、ビルドした `pfx-rat`：**GPL-3.0-only**（`LICENSE-GPL-3.0`）。
  dm-Rat（Dave Mollen、GPL-3.0）とschwung-303（Charles Vestal、GPL-3.0）からの移植だからです。
- 移植元・移植したファイル・変更点、参照しただけの実装（TS-808-Ultra、guitarix、ToneTwistPlugs、
  PedalKernel）とその扱い、参考にした回路解析と論文は `THIRD_PARTY_LICENSES.md` にすべて書いてあります。
  ToneTwistPlugsのMetalToneはリポジトリにLICENSEがなくDSPファイルにライセンス表記もないため、
  コードは一切使っていません。PedalKernel（AGPL-3.0+追加条項）は使っていません。
