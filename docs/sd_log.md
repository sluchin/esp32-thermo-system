# SD カードの記録の読み方

thermo-node が SD カードに保存する測定値の記録 (`backlog.bin`) について、形式と読み方をまとめます。実装は `app/thermo-node/src/backlog.c` です。

## 概要

- 全ての測定値 (5 秒ごと) を、1 つのファイルに 1 件ずつ追記します。
- 保存先は、SD カードの `/SD:/backlog.bin` です。FAT は長いファイル名に対応していないので、ファイル名は大文字で `BACKLOG.BIN` と表示されます。
- 1 件は 16 バイトの固定長です。ファイルの大きさを 16 で割ると、件数が分かります。
- 件数の上限は、`CONFIG_THERMO_BACKLOG_MAX_RECORDS` (既定は 100000 件。約 1.6 MB で、約 5.8 日分) です。上限に達したら、最も古い記録から上書きします (輪になっています)。
- 数値は、全てリトルエンディアンです。

## 1 件の形式

| 位置 (バイト) | 大きさ | 内容 | 型 | 説明 |
|:---|:---|:---|:---|:---|
| 0〜3 | 4 | 時刻 | 符号なし 32 ビット | 測った UNIX 時刻 [s] (UTC) |
| 4〜5 | 2 | 温度 | 符号つき 16 ビット | ℃ の 10 倍 (240 なら 24.0 ℃。負の値あり) |
| 6〜7 | 2 | 湿度 | 符号なし 16 ビット | % の 10 倍 (490 なら 49.0 %)。`0xFFFF` は湿度なし (温度だけのセンサ) |
| 8 | 1 | 送信済みフラグ | 符号なし 8 ビット | `00` は未送信、`01` は送信済み |
| 9〜10 | 2 | 気圧 | 符号なし 16 ビット | hPa の 10 倍 (10132 なら 1013.2 hPa)。`0` は気圧なし (気圧センサがない、または使えない) |
| 11 | 1 | 予約 | | 常に `00` |
| 12〜15 | 4 | CRC | 符号なし 32 ビット | 0〜7 バイト目と 9〜10 バイト目 (気圧) の CRC-32 (IEEE 802.3。zlib の `crc32` と同じ)。気圧なし (`0`) のときは、0〜7 バイト目だけの CRC-32 |

- CRC は、0〜7 バイト目と気圧にかけます。送信済みフラグ (8 バイト目) は含めません。送り終えたときに、フラグの 1 バイトだけを書き換えるためです。
- 気圧なしの記録の CRC は、0〜7 バイト目だけです。この形は、気圧を足す前のノードの記録 (9〜11 バイト目が予約で `00`) と同じなので、以前の記録もそのまま読めます。
- CRC が合わない記録は、壊れた記録として読み飛ばします (未送信にも数えません)。

### 送信済みフラグの意味

| 保存したとき | フラグ | 理由 |
|:---|:---|:---|
| BLE がゲートウェイにつながっている | `01` (送信済み) | ゲートウェイが AWS IoT Core へ送るため |
| BLE がつながっていない | `00` (未送信) | あとで WiFi と MQTT で送るため。送れたら `01` に書き換える |

ゲートウェイにつながっている間の記録は、`01` で保存するだけで、ノードは送りません。そのため、ゲートウェイがインターネットにつながっていないと、その分は AWS に届きません。

## 輪 (リング) の仕組み

- 上限に達していない間は、ファイルが 16 バイトずつ大きくなり、先頭から時刻の順に並びます。
- 上限に達したあとは、ファイルの大きさは変わりません。最も古い記録の場所に、新しい記録を上書きします。そのため、ファイルの中の並びは、時刻の順とは限りません (途中から新しい記録が始まります)。
- ファイルの中で最も新しい記録は、時刻が最大の記録です。その次の場所に、最も古い記録があります。
- 上書きは、最も古い記録の場所から行います。送信済みの記録を選んで消すわけではありません。古い記録ほど送り終えていることが多いので、ふつうは未送信の記録が最後まで残ります。ただし、未送信の記録が上限より多いと、古い未送信の記録も上書きされます。
- 起動のときに、ファイル全体を 1 回読んで、最も新しい記録の位置、未送信の数、最も古い未送信の記録を調べます (既定の 100000 件で数秒)。

## シリアルで見る

ノードのシェルで、次のコマンドを使います (`picocom -b 115200 /dev/ttyACM0`)。

```text
thermo sd                           SD カードが使えるか確かめる (使えなければマウントし直す)
fs statvfs /SD:                     容量と空き
fs ls /SD:                          ファイルの一覧 (BACKLOG.BIN の大きさ)
fs read /SD:/backlog.bin 64         先頭の 64 バイトを 16 進で表示
fs read /SD:/backlog.bin 64 1600    1600 バイト目 (101 件目) から 64 バイトを表示
```

- 引数は、`fs read <ファイル> [大きさ] [位置]` の順です。位置を 16 の倍数にすると、記録の先頭から読めます。
- `fs mount` と `fs hexdump` はありません。
- `fs cat /SD:/backlog.bin` は使わないでください (全部が画面に流れます)。
- `fs rm` と `fs write` も使えます。保存の途中に実行すると、ファイルが壊れるおそれがあります。

### 読み方の例

```text
00000000  DF 2B CC 6A F0 00 EA 01 01 00 00 00 CB 5A FD 97
```

| 項目 | バイト | 値 |
|:---|:---|:---|
| 時刻 | `DF 2B CC 6A` | 0x6ACC2BDF = 1791765471 (UNIX 時刻) |
| 温度 | `F0 00` | 0x00F0 = 240 で 24.0 ℃ |
| 湿度 | `EA 01` | 0x01EA = 490 で 49.0 % |
| 送信済みフラグ | `01` | 送信済み |
| 気圧 | `00 00` | 0 で気圧なし (気圧があれば、たとえば `94 27` = 0x2794 = 10132 で 1013.2 hPa。CRC も変わる) |
| 予約 | `00` | |
| CRC | `CB 5A FD 97` | 0x97FD5ACB (0〜7 バイト目の CRC-32) |

次の記録 (16 バイト後) の時刻の下位バイトが `E4` なら、5 秒後 (0xDF + 5 = 0xE4) です。

UNIX 時刻を日時にするには、PC で `date -u -d @1791765471` を実行します (UTC)。

## PC で読む

SD カードを PC に挿して、`BACKLOG.BIN` を読みます (カードは、ノードの電源を切ってから抜いてください)。次の Python で、全ての記録を表示します。

```python
#!/usr/bin/env python3
"""BACKLOG.BIN を読んで 1 行ずつ表示する (使い方: python3 read_backlog.py BACKLOG.BIN)."""
import struct
import sys
import zlib
from datetime import datetime, timezone

RECORD_SIZE = 16
HUMIDITY_NONE = 0xFFFF
PRESSURE_NONE = 0

with open(sys.argv[1], "rb") as f:
    data = f.read()

print(f"{len(data) // RECORD_SIZE} records ({len(data) % RECORD_SIZE} bytes left over)")
for i in range(len(data) // RECORD_SIZE):
    rec = data[i * RECORD_SIZE:(i + 1) * RECORD_SIZE]
    unix_s, temp, humi, flag, pres, crc = struct.unpack("<IhHBHxI", rec)
    # 気圧があれば 0~7 バイト目に続けて気圧の 2 バイトにも CRC をかける
    want = zlib.crc32(rec[:8]) if pres == PRESSURE_NONE else zlib.crc32(rec[9:11], zlib.crc32(rec[:8]))
    if crc != want:
        print(f"{i:6d}  BROKEN (CRC mismatch)")
        continue
    when = datetime.fromtimestamp(unix_s, timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    humi_text = "-" if humi == HUMIDITY_NONE else f"{humi / 10:.1f}"
    pres_text = "-" if pres == PRESSURE_NONE else f"{pres / 10:.1f}"
    print(f"{i:6d}  {when} UTC  {temp / 10:5.1f} C  {humi_text:>5} %  {pres_text:>6} hPa  "
          f"{'sent' if flag == 1 else 'NOT SENT'}")
```

- 件数が上限に達していると、並びは時刻の順ではありません。時刻で並べ替えて見てください (`sort` に通す、など)。
- 末尾に 16 バイトに満たない端があれば、書いている途中で電源が切れたことを示します。この端は、起動のときに切り捨てます。

## 壊れたときの扱い

| 状態 | 動作 |
|:---|:---|
| 書きかけの記録 (16 バイトに満たない端) | 起動のときに無視する (次の記録で上書きする) |
| CRC が合わない記録 | 読み飛ばす。未送信にも数えない |
| ファイルがない | 最初の記録を保存するときに作る |
| 古い形式のファイル (以前の `backlog.bin` と `backlog.pos`) | 新しい形式の記録としては読めない。カードに残っていれば消す (`fs rm /SD:/backlog.bin`、`fs rm /SD:/backlog.pos`) |
| SD カードがない、または FAT でない | 保存しない。ほかの機能 (BLE、OLED) は続ける |

## ログのファイル (boot.log と error.log)

測定値の記録 (`backlog.bin`) とは別に、ノードのログ (シリアルに出るログと同じ文字列) を、テキストで SD カードに保存します。実装は、Zephyr のログのバックエンドの `app/thermo-node/src/sdlog.c` です。

| ファイル (FAT の表示) | 内容 |
|:---|:---|
| `/SD:/boot.log` (`BOOT.LOG`) | 起動から 60 秒の間の、全てのレベル (`<inf>` `<wrn>` `<err>`) のログ。起動のたびに作り直します (前の起動のログは消えます) |
| `/SD:/error.log` (`ERROR.LOG`) | エラー (`<err>`) のログ。60 秒のあとも書き続け、起動をまたいで追記します |
| `/SD:/error.old` (`ERROR.OLD`) | `error.log` が 64 KB に達したときの、古い `error.log` (1 世代だけ。その前の `error.old` は消えます) |

- 1 行の形は、シリアルのログと同じです (例: `[00:00:12.345,000] <err> sensor_thermo_node: Could not fetch the sensor (-5)`)。時刻は、起動からの時間です (UNIX 時刻ではありません)。
- ログは、いったん RAM に溜めて、メインループが 1 秒ごとに書きます。SD カードをマウントできるまでは RAM に溜めたままにして、マウントできたときに書きます。電源が切れると、直前の 1 秒ほどのログは失われます。
- RAM に溜め切れなかったログは捨てて、`The boot log buffer was full, N bytes were not saved` のようなエラーを出します。
- 書き込みに失敗したとき (カードが外れた、容量がない) は、`Could not write the log to the SD card` を出して、書き込みを止めます。`thermo sd` でマウントし直すと、`SD card ready` のときに再開します。
- 設定: `CONFIG_THERMO_SDLOG` (保存するか)、`CONFIG_THERMO_SDLOG_BOOT_SEC` (60)、`CONFIG_THERMO_SDLOG_BOOT_BUF_SIZE` (768)、`CONFIG_THERMO_SDLOG_ERROR_BUF_SIZE` (256)、`CONFIG_THERMO_SDLOG_ERROR_MAX_BYTES` (65536)。保存しないときは、`-DCONFIG_THERMO_SDLOG=n` でビルドします。
- 読み方: ノードのシェルで `fs cat /SD:/boot.log` を実行するか、SD カードを PC に挿して読みます。`fs cat` は書き込み中でも使えますが、`fs rm` は書き込み中には実行しないでください。

## 関連する文書

- [setup.md](setup.md): SD カードの配線とシェルの使い方
- [test_integration.md](test_integration.md): 結合試験項目書 (SD カードの試験を含む)
