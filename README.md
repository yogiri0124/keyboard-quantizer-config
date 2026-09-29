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
| `upstream.env` | 土台にする本家のコミットと、ビルド対象 |

ここにあるのは本家と異なるファイルだけ。それ以外は本家のものがそのまま使われる。
QMK 本体のファイルはまるごと持たず、`patches/` の差分として当てる。本家が同じ箇所を
変えると `git apply` が失敗してビルドが止まるので、本家の修正を黙って巻き戻すことがない。

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
