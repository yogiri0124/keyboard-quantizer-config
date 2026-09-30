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
| `keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial/` | キーマップ、マウス処理、CLI、キーオーバーライド |
| `keyboards/sekigon/keyboard_quantizer/mini/matrix.c`, `info.json` | 基板側の処理と定義 |
| `keyboards/sekigon/keyboard_quantizer/parser/` | 接続したデバイスの HID レポート解析 |
| `util/vial_generate_definition.py` | Vial 定義の生成 |
| `patches/` | QMK 本体への小さな修正（`git apply` する差分） |
| `tools/hid_descriptors.py` | ビルドした `.elf` の HID 記述子を検証・表示（CI が実行） |
| `upstream.env` | 土台にする本家のコミットと、ビルド対象 |

ここにあるのは本家と異なるファイルだけ。それ以外は本家のものがそのまま使われる。
QMK 本体のファイルはまるごと持たず、`patches/` の差分として当てる。本家が同じ箇所を
変えると `git apply` が失敗してビルドが止まるので、本家の修正を黙って巻き戻すことがない。

## スクロール

ホイールとボールスクロールは、HID の高分解能ホイール（Resolution Multiplier ×120）で送る。
Windows / Linux が有効にすると 1 ノッチが 120 カウントに分かれ、対応するアプリでは
ボールスクロールが 1/120 ノッチ単位で滑らかに動く。有効にしないホスト（macOS など）には
従来どおり 1 ノッチ単位で送る。

- 物理ホイール・WH_* キー：1 ノッチをそのまま送る（加工なし）。
- ボールスクロール（SCR）：ボールのカウントをスクロール除数で割った量を、端数も捨てずに送る。
- 修飾キーを押しながらのホイール（Ctrl + ホイールなど）、修飾付きの WH_* キー、マクロ：
  その場で送り切り、マウス側の USB 送信が終わるのを待ってから戻る（最大 10 ms）。
  修飾キーの解除がホイールを追い越さないようにするため。ホスト側でのエンドポイント間の
  処理順までは保証できない。

出力は `keymaps/vial/scroll_out.c` が整数で管理する（1 レポート ±127 を超える分は次のレポートへ）。
QMK 本体側の変更は `patches/0002`（記述子と倍率の Feature 要求）と `patches/0003`
（mousekey の送信をキーマップ側で受け取るフック）。

| patch | 内容 |
| --- | --- |
| `0001` | 仮想シリアル（CLI）が短い入力を捨てる不具合の修正 |
| `0002` | ホイールの Resolution Multiplier（記述子、Feature の GET/SET、倍率の保持） |
| `0003` | mousekey のレポートをキーマップ側で受け取るフック |

## 本家の更新を取り込む

`upstream.env` の `UPSTREAM_REF` を新しいコミットに書き換えて push する。
ビルドが通らなくなったら、そのコミットで本家側が変わったということなので、
`UPSTREAM_REF` を戻せば元に戻る。

## 手元でビルドしたいとき

```sh
git clone --recurse-submodules https://github.com/sekigon-gonnoc/vial-qmk.git
cd vial-qmk
git checkout <upstream.env の UPSTREAM_REF>
cp -a /path/to/keyboard-quantizer-config/keyboards /path/to/keyboard-quantizer-config/util .
for p in /path/to/keyboard-quantizer-config/patches/*.patch; do git apply "$p"; done
make sekigon/keyboard_quantizer/mini:vial
```
