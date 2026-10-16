# keyboard-quantizer-config

Keyboard Quantizer mini 専用のファームウェア設定。QMK 本体は持たず、ビルド時に
GitHub Actions が本家 [sekigon-gonnoc/vial-qmk](https://github.com/sekigon-gonnoc/vial-qmk)
を取得し、このリポジトリのファイルを上書きしてビルドする。ZMK config と同じ考え方。

## 使い方

1. ファイルを編集して push する。
2. Actions の実行が終わったら、成果物 `keyboard_quantizer_mini_vial` をダウンロードする。
3. Quantizer をブートローダーに入れ（CLI で `dfu`、または BOOTSEL を押しながら接続）、
   出てきた `RPI-RP2` ドライブに `.uf2` をコピーする。

## 中身

| パス | 役割 |
| --- | --- |
| `keyboards/sekigon/keyboard_quantizer/parser/` | HID レポート記述子のコンパイルとレポートの解読（`hid_desc` / `hid_report`） |
| `keyboards/sekigon/keyboard_quantizer/mini/` | 基板側：USB ホストのイベントキュー（`hid_queue`）、入力からマトリクスへ（`hid_input`）、TinyUSB コールバックと LED（`hid_host`）、`matrix.c` |
| `keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial/` | キーマップ側（下の表） |
| `patches/` | QMK 本体への小さな修正（`git apply` する差分） |
| `tools/test/` | ホストで動かすテスト（CI が実行） |
| `tools/hid_descriptors.py` | ビルドした `.elf` の HID 記述子を検証・表示（CI が実行） |
| `util/vial_generate_definition.py` | Vial 定義の生成 |
| `upstream.env` | 土台にする本家のコミットと、ビルド対象 |

キーマップ側（`keymaps/vial/`）：

| ファイル | 役割 |
| --- | --- |
| `keymap.c` | 恒等キーマップと QMK のフック |
| `mouse.c` | ボールの行き先（カーソル / スクロール / ジェスチャー）と PC に送るマウスレポート |
| `mouse_keys.c` | マウス用キーの処理と「次のキー」で解除する規則 |
| `modes.c` | スクロール・ジェスチャー・スローのモード（押している間 / 切り替え / 次のキーまで） |
| `gesture.c` | ジェスチャーの判定（8 方向） |
| `pointer.c` | カーソルの倍率・スロー・跳ね返り除去 |
| `wheel.c` | ホイールとチルト、WH_* キー、mousekey のレポート |
| `scroll_out.c` | スクロール出力（高分解能ホイール） |
| `locks.c` | ドラッグロックと修飾キーロック |
| `settings.c` | 保存する設定（Vial のマウスメニュー、CLI、SCR+ などのキー） |
| `macro.c` | ダイナミックマクロの再生 |
| `cli/`, `key_override/`, `raw_hid.c` | 仮想シリアルの CLI、JP/US 配列変換、VIA/Vial の振り分け |

`parser/` と `keymaps/vial/` はこのリポジトリが丸ごと持つ（CI が本家の同じディレクトリを
消してから上書きする）。それ以外は本家と異なるファイルだけを置き、残りは本家のものを使う。
QMK 本体のファイルはまるごと持たず、`patches/` の差分として当てる。本家が同じ箇所を
変えると `git apply` が失敗してビルドが止まるので、本家の修正を黙って巻き戻すことがない。

## 入力の流れ

接続したデバイスの USB は core1 が受け、マウント・切断・レポートを受けた順に 1 本の
キューで core0 に渡す（`hid_queue`）。キーマップ側が長いマクロなどで止まってキューが
埋まった場合は、インターフェースごとに最新のレポートだけを取っておき、順番どおりの
位置で渡す（キーやボタンは最新の状態が残るので押しっぱなしにならない）。

core0 はレポートを記述子の表で解読し（`parser/`）、レポートごとの状態をまとめて
マトリクスにする（`hid_input`）。キーボードを 2 台つないでも互いのキーを消さない。
1 回のスキャンで変えるのはマトリクス 1 回分だけなので、素早い押して離すも消えない。
メディアキー（音量など）とシステムキーは押下・解放をそのまま PC に送る。

デバイスごとの癖：ELECOM HUGE PLUS はボタン 6〜8 を「パディング」のビットで送る、
左チルトを正の値で送る。Kensington SlimBlade はボタン 3・4 をベンダー領域で送る。
Lenovo ThinkPad キーボードは横スクロールをベンダーレポートで送る。どれも読み替えている。

## ジェスチャー

ジェスチャー中（GES / T.GES / O.GES、またはオレンジマスだけ割り当てたレイヤー）は、
ボールを動かした方向のオレンジマスのキーを押す。カーソルは動かない。

- 8 方向：上下左右と斜め 4 つ。斜めのマスが空なら、斜めの動きは近い上下左右として扱う。
- 1 回分の距離（マウスメニューの「ジェスチャー1回分の距離」、THR- / THR+）だけ動くと
  1 回押し、動かし続けると 1 回分の距離ごとにまた押す。ゆっくりなら少なく、速く弾けば多く。
- 方向は動き始めに一度だけ決め、ボールが止まる（約 0.12 秒）まで変えない。
  方向の境目付近の動きは、はっきりするまで少し待つ。逆向きに動かしても何も起きない。
- 連続は 40 ms に 1 回まで、待ちは 3 回まで。強く弾いても止めた後に長く出続けない。

## スクロール

ホイールとボールスクロールは、HID の高分解能ホイール（Resolution Multiplier ×120）で送る。
Windows / Linux が有効にすると 1 ノッチが 120 カウントに分かれ、対応するアプリでは
ボールスクロールが 1/120 ノッチ単位で滑らかに動く。有効にしないホストには
従来どおり 1 ノッチ単位で送る。

- 物理ホイール・チルト：Vial のマス（28,1〜28,4）を押す。WH_* が入っていれば回した量を
  そのまま送り、同じ軸の逆向きなら反転、別の軸ならその向きに送る。別のキーならそのキー。
- WH_* キー：1 ノッチ、押し続けると QMK Settings のホイールの遅延・間隔でくり返す。
- ボールスクロール（SCR）：ボールのカウントをスクロール除数で割った量を、端数も捨てずに送る。
- 修飾キーを押しながらのホイール（Ctrl + ホイールなど）、修飾付きの WH_* キー、マクロ：
  その場で送り切り、マウス側の USB 送信が終わるのを待ってから戻る（最大 10 ms）。
  修飾キーの解除がホイールを追い越さないようにするため。ホスト側でのエンドポイント間の
  処理順までは保証できない。

| patch | 内容 |
| --- | --- |
| `0001` | 仮想シリアル（CLI）が短い入力を捨てる不具合の修正 |
| `0002` | ホイールの Resolution Multiplier（記述子、Feature の GET/SET、倍率の保持） |
| `0003` | mousekey のレポートをキーマップ側で受け取るフック |

## CLI

仮想シリアル（COM ポート）で次のコマンドが使える。

| コマンド | 内容 |
| --- | --- |
| `version` | ファームウェアのバージョン |
| `mouse`、`mouse reset`、`mouse scroll <値>`（`dpi`・`thresh` も同様） | マウス設定の表示・初期化・変更 |
| `hid` | 受信の統計（ホイールの向き、PC が選んだ倍率、キューのあふれ） |
| `hid desc` | 接続デバイスの記述子を解読した表 |
| `ovr off`、`ovr uj`、`ovr ju` | JP/US 配列変換 |
| `piousb`, `debug`, `dfu` | USB の通信エラー率、デバッグ出力、ブートローダー |

## テスト

`tools/test/run.sh` が、ファームウェアのうち QMK に依存しない部分（パーサ、キュー、
入力からマトリクス、モード、カーソル、ジェスチャー、スクロール出力）をホスト向けに
コンパイルして確かめる。CI はビルドの前に実行する。手元では

```sh
sh tools/test/run.sh
```

（`cc` が無ければ `CC=gcc` などを指定する。）

## 本家の更新を取り込む

`upstream.env` の `UPSTREAM_REF` を新しいコミットに書き換えて push する。
ビルドが通らなくなったら、そのコミットで本家側が変わったということなので、
`UPSTREAM_REF` を戻せば元に戻る。

## 手元でビルドしたいとき

```sh
git clone --recurse-submodules https://github.com/sekigon-gonnoc/vial-qmk.git
cd vial-qmk
git checkout <upstream.env の UPSTREAM_REF>
git submodule update --init --recursive
rm -rf keyboards/sekigon/keyboard_quantizer/parser keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial
cp -a /path/to/keyboard-quantizer-config/keyboards /path/to/keyboard-quantizer-config/util .
for p in /path/to/keyboard-quantizer-config/patches/*.patch; do git apply "$p"; done
make sekigon/keyboard_quantizer/mini:vial
```
