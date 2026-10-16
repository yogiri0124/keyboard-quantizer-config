# keyboard-quantizer-config

Keyboard Quantizer mini（sekigon-gonnoc 作、以下 Quantizer）用の
ファームウェアです。Quantizer は、USB のキーボードやマウスと PC の間につなぐ小さなアダプタで、
つないだ機器のキーやボタンを [Vial](https://get.vial.today/) で自由に割り当て直せるようにします。

このファームウェアは、トラックボール **ELECOM HUGE PLUS** をつないで使うことを中心に作っています
（ほかのキーボードやマウスもそのまま使えます）。

## できること

- **キーとボタンの割り当て**：つないだキーボードのキー、マウスのボタン・ホイール・チルトを、
  Vial で好きなキーやマクロ、レイヤー切り替えに変えられる。キーボードを 2 台つないでも動く。
- **ボールでスクロール**：キーを押している間（または切り替えて）ボールでスクロールする。
  Windows / Linux では 1/120 ノッチ単位の高分解能スクロールになり、滑らかに動く。
- **ジェスチャー**：ボールを動かした方向（上下左右と斜めの 8 方向）に割り当てたキーを押す。
  動かし続けるとくり返す。カーソルは動かない。
- **カーソルの速さ**：常時の倍率と、押している間だけ遅くするキー。
- **ドラッグロック・修飾キーロック**：クリックや Ctrl などを押しっぱなしにするキー。
- **設定は保存される**：書き込み直しても Vial の設定は消えない。初期状態は `default.vil` の内容。

## 使い始める

### 1. ファームウェアを入手する

GitHub の **Actions** タブで最新の成功したビルドを開き、成果物
`keyboard_quantizer_mini_vial` をダウンロードして展開すると `.uf2` ファイルが入っている。

### 2. Quantizer に書き込む

1. Vial を閉じる。
2. Quantizer をブートローダーに入れる。次のどちらか。
   - 仮想シリアル（COM ポート）に `dfu` と送る（下の「CLI」）。
   - BOOTSEL ボタンを押しながら PC につなぐ。
3. PC に `RPI-RP2` というドライブが出るので、`.uf2` をコピーする。自動で再起動する。

### 3. Vial で設定する

Vial で Quantizer を開き、Layout を **HUGE PLUS** にすると、HUGE PLUS の形でキーが並ぶ。

| マスの色 | 中身 |
| --- | --- |
| 灰色 | マウスのボタン（Mouse1〜8） |
| 青 | ホイール上・下（28,1 / 28,2）とチルト左・右（28,3 / 28,4）。1 ノッチごとにそのマスのキーが押される。WH_Up / WH_Down などを入れればスクロール、逆向きを入れれば反転 |
| 緑 | ボールの方向のマス。ボールでは押されない。ここを空にしたレイヤーでは、オレンジのマスを割り当てるだけでジェスチャーになる |
| オレンジ（濃い） | ジェスチャーの上下左右 |
| オレンジ（薄い） | ジェスチャーの斜め。空なら、斜めの動きは近い上下左右として扱う |

Layout を **ALL** にすると、つないだキーボードのすべてのキーが並ぶ。各マスは HID の使用番号
（Usage）の順で、初期状態ではキーがそのままのキーを送る。

ジェスチャーのマス（オレンジ）は、使うレイヤー（1〜7）に割り当てる。レイヤー 0 では
レイヤー 1 のマスが使われる。透過（▽）のマスは下のレイヤーのマスになる。

### マウス用のキー

Vial の「User」タブにある。押している間だけ効くもの、押すたびにオン/オフするもの、
次のキーまで効くもの（ワンショット）がある。

| キー | 働き |
| --- | --- |
| `SCR` / `D.SCR` / `O.SCR` | ボールスクロール（押している間 / オン・オフ / 次のキーまで） |
| `S.SCR` | スクロールをさらに遅くする（押している間） |
| `SCR+` / `SCR-` | ボールスクロールを速く / 遅く（保存される） |
| `GES` / `T.GES` / `O.GES` | ジェスチャー（押している間 / オン・オフ / 次のキーまで） |
| `THR-` / `THR+` | ジェスチャーを敏感に / 鈍く：1 回分の距離を短く / 長く（保存される） |
| `SLOW` / `T.SLO` / `O.SLO` | カーソルを遅くする（押している間 / オン・オフ / 次のキーまで） |
| `DPI+` / `DPI-` | カーソルを速く / 遅く（保存される） |
| `D.LCK` / `D.MID` / `D.RCK` | 左 / 中 / 右クリックを押しっぱなしにする（もう一度押すか、そのボタンで解除） |
| `C.LCK` / `S.LCK` / `A.LCK` / `W.LCK` | Ctrl / Shift / Alt / Win を押しっぱなしにする |
| `SHOW` | 今の設定を文字で入力する（テキスト欄で使う） |
| `CLR` | スクロール・ジェスチャー・スロー・ロックをすべて解除する |
| `RST` | `CLR` に加えて、速さとジェスチャーの距離を初期値に戻す |

タップダンスの TAP にモードキーを入れると「次のキーまで」、HOLD に入れると「押している間」に
なる。クリック、ホイール、レイヤーキー、マウスの設定キー、修飾キーロックは「次のキー」に数えない。

### ジェスチャーの動き

- ボールを「1 回分の距離」動かすと、その方向のキーを 1 回押す。動かし続けると、1 回分の距離ごとに
  また押す。ゆっくりなら少なく、速く弾けば多く出る。
- 方向は動き始めに決まり、ボールが止まるまで（約 0.12 秒）変わらない。手がぶれても隣の方向に
  ならない。方向の境目に近い動きは、はっきりするまで少し待つ。逆向きに動かしても何も起きない。
- 連続は 40 ms に 1 回まで。強く弾いても、止めた後に長く出続けない。

### 速さなどの設定

Vial の「マウス」メニューのスライダーで変えられる（キーでも変えられる）。

| 項目 | 範囲 | 初期値 | 意味 |
| --- | --- | --- | --- |
| スクロール除数 | 8〜80 | 32 | ボールのカウントいくつで 1 ノッチか。小さいほど速い |
| カーソル倍率 | 4〜32 | 16 | 16 で等倍。大きいほど速い |
| ジェスチャー 1 回分の距離 | 20〜120 | 50 | 小さいほど少し動かしただけで出て、多く出る |

## 初期設定（default.vil）を変える

`keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial/default.vil` が、書き込んだ直後や
リセット後の設定（キー配置、タップダンス、コンボ、マクロ、QMK Settings）になる。
Vial の「File → Save current layout」で保存したファイルで置き換えて push すればよい。

保存した設定は書き込み直しても残る。保存形式が変わる更新（本家 QMK の取り込みなど）の
ときだけ初期設定に戻る。

## CLI

Quantizer は仮想シリアル（COM ポート）でもつながる。ターミナルソフト（115200 bps）で
次のコマンドが使える。

| コマンド | 内容 |
| --- | --- |
| `version` | ファームウェアのバージョン |
| `mouse` | マウスの設定とモード・ロックの状態 |
| `mouse scroll 32`、`mouse dpi 16`、`mouse thresh 50`、`mouse reset` | 設定の変更・初期化 |
| `hid` | 受け取った入力の統計（ホイールの向き、PC が選んだスクロールの細かさなど） |
| `hid desc` | つないだ機器の HID 記述子を読み解いた表 |
| `ovr off`、`ovr uj`、`ovr ju` | US 配列と JP 配列の読み替え（uj：US キーボードを JP 設定の PC で、ju：その逆） |
| `piousb` | USB 通信のエラー率 |
| `debug` | デバッグ出力のオン/オフ |
| `dfu` | ブートローダーに入る（書き込み用） |

## 仕組み（改造する人向け）

QMK 本体は持たず、GitHub Actions がビルドのたびに本家
[sekigon-gonnoc/vial-qmk](https://github.com/sekigon-gonnoc/vial-qmk)（`upstream.env` のコミット）を取得し、
このリポジトリのファイルを上書きし、`patches/` を当ててビルドする。push すれば Actions が
テストとビルドを行う。

### ファイル

| パス | 役割 |
| --- | --- |
| `keyboards/sekigon/keyboard_quantizer/parser/` | つないだ機器の HID 記述子を表にし（`hid_desc`）、届いたレポートを読み解く（`hid_report`） |
| `keyboards/sekigon/keyboard_quantizer/mini/` | USB ホスト（core1）から core0 へのキュー（`hid_queue`）、入力からキーの行列へ（`hid_input`）、USB のコールバックと LED（`hid_host`） |
| `.../mini/keymaps/vial/` | Vial 用キーマップ（下の表）、`vial.json`（Vial の画面）、`default.vil`（初期設定） |
| `patches/` | QMK 本体への小さな修正 |
| `tools/test/` | パソコン上で動かすテスト |
| `tools/vil_to_c.py` | `default.vil` をファームウェアの初期設定に変換する |
| `tools/hid_descriptors.py` | ビルドした `.elf` の HID 記述子を検証する |
| `util/build_id.py` | 保存した設定を書き込み直しても残すための識別子 |
| `upstream.env` | 土台にする本家のコミット |

`keymaps/vial/` の中身：

| ファイル | 役割 |
| --- | --- |
| `keymap.c` | QMK のフック、キーの検索 |
| `mouse.c` | ボールの行き先（カーソル / スクロール / ジェスチャー）と、PC に送るマウスの報告 |
| `mouse_keys.c` | マウス用のキーと、「次のキー」で解除する規則 |
| `modes.c` | スクロール・ジェスチャー・スローのモード |
| `gesture.c` | ジェスチャーの判定 |
| `pointer.c` | カーソルの倍率・スロー・跳ね返り除去 |
| `wheel.c` | ホイールとチルト、WH_* キー |
| `scroll_out.c` | 高分解能スクロールの出力 |
| `locks.c` | ドラッグロックと修飾キーロック |
| `settings.c` | 保存する設定（マウスメニュー、CLI、SCR+ などのキー） |
| `default_config.c` | `default.vil` の初期設定を書き込む |
| `macro.c`, `cli/`, `key_override/`, `raw_hid.c` | マクロの再生、CLI、JP/US 読み替え、VIA/Vial の通信 |

### 入力の流れ

1. つないだ機器の USB は core1 が受け、マウント・切断・レポートを届いた順にキューで core0 に渡す。
   core0 が長いマクロなどで止まってキューが埋まったら、レポートの種類ごとに最新のものだけ残し、
   順番どおりの位置で渡す（キーやボタンの状態は最新のものが残るので、押しっぱなしにならない）。
2. core0 はレポートを記述子の表で読み解き、レポートごとのキー・ボタンの状態をまとめて
   キーの行列（Vial のマス）にする。1 回の走査で変えるのは 1 回分だけなので、素早い
   押して離すも消えない。音量などのメディアキーは押下・解放をそのまま PC に送る。
3. QMK がキー配置に従ってキーを処理する。ボールとホイールの動きは `mouse.c` が受ける。

機器ごとの癖も読み替えている：ELECOM HUGE PLUS はボタン 6〜8 を「詰め物」のビットで送り、
左チルトを正の値で送る。Kensington SlimBlade はボタン 3・4 をベンダー領域で送る。
Lenovo ThinkPad キーボードは横スクロールをベンダーのレポートで送る。

### QMK 本体への修正（patches/）

| patch | 内容 |
| --- | --- |
| `0001` | 仮想シリアル（CLI）が短い入力を捨てる不具合の修正 |
| `0002` | 高分解能ホイール（Resolution Multiplier）：記述子、PC からの倍率の設定、倍率の保持 |
| `0003` | mousekey の報告をキーマップ側で受け取るフック |
| `0004` | タップホールド判定待ちのキューの空きと、保留中のコンボのキー数を返す関数（ホイールとジェスチャーのタップがキューをあふれさせないため） |

本家の同じ箇所が変わると `git apply` が失敗してビルドが止まるので、本家の修正を黙って
巻き戻すことはない。

### テスト

`tools/test/run.sh` が、QMK に依存しない部分（記述子とレポートの読み解き、キュー、
入力から行列、モード、カーソル、ジェスチャー、スクロール出力）をパソコン向けに
コンパイルして確かめる。Actions はビルドの前に実行する。手元では

```sh
sh tools/test/run.sh
```

（`cc` が無ければ `CC=gcc` を付ける。）

### 本家の更新を取り込む

`upstream.env` の `UPSTREAM_REF` を新しいコミットに書き換えて push する。ビルドが
通らなければ、本家側が変わったということなので、`UPSTREAM_REF` を戻せば元に戻る。

### 手元でビルドする

```sh
git clone --recurse-submodules https://github.com/sekigon-gonnoc/vial-qmk.git
cd vial-qmk
git checkout <upstream.env の UPSTREAM_REF>
git submodule update --init --recursive
rm -rf keyboards/sekigon/keyboard_quantizer/parser keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial
cp -a /path/to/keyboard-quantizer-config/keyboards /path/to/keyboard-quantizer-config/util .
for p in /path/to/keyboard-quantizer-config/patches/*.patch; do git apply "$p"; done
vial=keyboards/sekigon/keyboard_quantizer/mini/keymaps/vial
python3 /path/to/keyboard-quantizer-config/tools/vil_to_c.py $vial/default.vil quantum/vial_ensure_keycode.h > $vial/default_config.h
make sekigon/keyboard_quantizer/mini:vial
```
