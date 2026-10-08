# MISRA-C 例外規定書 (MISRA-C Deviations)

本ドキュメントは、BLE サーモメータシステム (`thermo-node` / `thermo-gateway`) における MISRA-C (MISRA C:2012) ガイドラインの適用方針と、プロジェクトの目的・アーキテクチャ上の理由から**あえて対応せず例外 (Deviation) としているルール**を定義・記録したものです。

コーディング規約の詳細は [CODING_STYLE.md](CODING_STYLE.md) を参照してください。

---

## 1. 概要と適用方針

本ソフトウェアは、ESP32C3 (Seeed Studio XIAO ESP32C3) 上で動作する Zephyr RTOS ベースの BLE ファームウェアです。

- `thermo-node`: DHT11 (または ADC) で温度と湿度を読み取り、BLE でアドバタイズする。
- `thermo-gateway`: BLE のアドバタイズをスキャンし、周辺ノード (と SwitchBot の温湿度計) のデータを集める。設定すると、WiFi と NTP で時刻を合わせ、MQTT (TLS) で AWS IoT Core に送る。

### 対象範囲

- **対象**: `app/` 配下のアプリケーションの C ソース (`app/thermo-node/src/`, `app/thermo-gateway/src/`)。
- **対象外**: 単体テスト (`app/*/tests/`)、Zephyr RTOS 本体、Bluetooth スタック、HAL (`hal_espressif`) などのサードパーティのコード。ただし、テストにも、同じ警告オプションを付けて、警告ゼロを維持します。シミュレーション (`native_sim`) で使うホストの libc も対象外です。

Zephyr のヘッダは、GNU 拡張と、多くのマクロを使います。マクロはアプリ側のコードに展開されるため、`#pragma` で囲むだけでは警告を抑えられません。そのため `app/warnings.cmake` で、Zephyr の include ディレクトリをシステムヘッダとして扱います (警告オプションは、アプリのソースだけに付けます)。

### 準拠・対応している主なプラクティス

- **制御式の本質的ブール型評価 (Rule 14.4)**: 条件式は bool だけにする。整数やポインタは、比較して bool にする (`if (err != 0)`, `if (ptr != NULL)`)。bool の値は、そのまま書く (`if (ready)`, `if (!ready)`。`== false` とは比較しない)。
- **条件式の中の関数呼び出し (Rule 13.5 の趣旨)**: 副作用のある関数 (`bt_enable()`, `sensor_init()`, `adc_read_dt()`, `k_sleep()` など、状態を変える、書き込む、開始・初期化・設定する関数) は、先に呼び出して、戻り値を変数に入れてから比較する。
  - **許容している書き方**: 判定だけをする関数 (状態を調べるだけで、何も変えない関数。`adc_is_ready_dt()`, `device_is_ready()`, `strcmp()` など) は、戻り値を変数に入れずに、そのまま条件式に書く。厳密には、変数に入れてから判定するべきだが、読みやすさのために許容している (Rule 13.5 は、`&&` / `||` の右側に、副作用のある関数呼び出しを書くことを禁じるもので、単独の判定は、対象外)。
- **戻り値のリテラル直書きの排除**: `return 0;` / `return -1;` を使わず、`EXIT_SUCCESS` / `EXIT_FAILURE` / `-ENODEV` などの名前付き定数を使う。
- **マジックナンバーの排除**: 時間、解像度、範囲などは `#define` で名前を付ける。
- **ローカル変数の初期化 (Rule 9.1)**: 宣言時に必ず初期化する。
- **`extern` 宣言の排除 (Rule 8.5)**: `.c` ファイルに `extern` 宣言を書かず、ヘッダに置いてインクルードする。
- **符号なし整数リテラルの `U` サフィックス (Rule 7.2)**: 符号なし型で使う整数リテラルに、大文字の `U` を付ける (`uint16_t temp_raw = 0U;`)。
- **`long` 系リテラルのサフィックス**: `long` / `ssize_t` には `L`、`long long` には `LL` (大文字。小文字の `l` は Rule 7.3 により使わない)。
- **予約済み識別子の回避 (Rule 21.1 / 21.2)**: ヘッダのインクルードガードは、アンダースコアで始めない (`THERMO_NODE_BLE_H`)。
- **関数戻り値の確認 (Rule 17.7)**: 戻り値を返す関数は必ず検証する。あえて無視する場合は `(void)` キャストを明示する (`(void)k_sleep(...)`)。
- **標準入出力ライブラリを使わない (Rule 21.6)**: ログは Zephyr Logging API (`LOG_INF` / `LOG_ERR` など) で出力し、`printf` / `printk` は使わない。
- **動的メモリを使わない (Rule 21.3)**: アプリのコードでは、`malloc` / `free` を使わない (静的確保のみ)。
- **再帰、`goto`、シグナル、`exit()` / `atexit()`、可変長引数、浮動小数点を使わない**: アプリのコードは、これらの Rule 15.1, 17.2, 21.5, 21.8, 17.1, 14.1 の対象となる機能を使いません。
- **到達しないコードの排除 (Rule 2.1)**: 無限ループの後ろに、到達しない `return` を書かない。
- **コンパイル警告ゼロ**: `app/warnings.txt` の警告オプション (`-Wall` `-Wextra` `-Wpedantic` `-Wconversion` `-Wsign-conversion` など約 55 個) で警告が出ない状態を維持。
- **静的解析**: `docker compose run --rm analyze-thermo-node` / `analyze-thermo-gateway` (gcc `-fanalyzer`) で、指摘が出ない状態を維持。

---

## 2. 例外事項 (Deviation) サマリー一覧

| No. | MISRA C:2012 | 分類 | 概要 | プロジェクトでの主な該当箇所 |
|:---:|:---|:---:|:---|:---|
| 1 | Rule 1.2 | Advisory | 言語拡張 (GNU 拡張) の使用 | Zephyr API のマクロ (`LOG_*`, `BT_DATA*`, `K_SECONDS`, `BIT`, `ARRAY_SIZE`, `ADC_DT_SPEC_GET`) |
| 2 | Dir 4.9 | Advisory | 関数形式マクロの使用 | Zephyr API のマクロ (上記と同じ) |
| 3 | Rule 20.7, 20.10, 20.12 | Required / Advisory | マクロ引数の括弧、`#` / `##` 演算子、マクロ引数の展開 | `LOG_MODULE_REGISTER`, Devicetree マクロ (`DT_*`) |
| 4 | Rule 11.3 | Required | オブジェクトポインタの型のキャスト | `BT_DATA` が文字列を `uint8_t *` として扱う (`app/thermo-node/src/ble.c`)。API の引数の型に合わせる文字列と配列のキャスト (`cloud.c`, `wifi_link.c`, `ble.c`) |
| 5 | Rule 15.5 | Advisory | 早期リターン (単一終了点規則の例外) | エラー時の `return err;` (`ble.c`, `sensor.c`, `main.c`) |
| 6 | Rule 14.3 | Required | 不変な制御式 (`while (true)`) | ファームウェアの無限ループ (`main.c`) |
| 7 | Rule 15.6 | Required | 制御構文の本体を `{}` で囲まない (本体が 1 文のとき) | 1 文の `if` / `else` / `for` / `while` (`CODING_STYLE.md` の例外 5) |
| 8 | Rule 18.4 | Advisory | ポインタへの整数の加算 | 通知データの 2 つ目の値の位置 (`app/thermo-gateway/src/ble.c`) |
| 9 | Rule 22.8 - 22.10 | Required | `errno` の参照 | ソケットの `poll()` の失敗の原因 (`app/thermo-gateway/src/cloud.c`) |

---

## 3. 各例外事項の詳細と安全対策 (Deviations and Mitigations)

### 例外 1: 言語拡張 (GNU 拡張) の使用 (Zephyr API)
- **該当ルール**: MISRA C:2012 Rule 1.2 (Advisory)
  - 「言語拡張を使用してはならない」
- **理由 (Rationale)**:
  - Zephyr の API は、GNU C の拡張 (式文、`typeof`、`__attribute__` など) を使ったマクロで提供されます。ログ (`LOG_INF` など)、BLE のアドバタイズデータ (`BT_DATA_BYTES`)、時間 (`K_SECONDS`)、Devicetree (`ADC_DT_SPEC_GET`) は、マクロを通して使うことが前提です。
- **安全対策 (Mitigation)**:
  - アプリのコードでは、GNU 拡張を、直接は書きません。拡張は、Zephyr のマクロの内側だけに現れます。
  - `-Wpedantic` を含む警告オプションで、アプリのコードに拡張が入り込んでいないことを確認します。Zephyr のヘッダは、システムヘッダとして扱い、アプリ側では直せない警告を出さないようにしています。

### 例外 2: 関数形式マクロの使用
- **該当ルール**: MISRA C:2012 Dir 4.9 (Advisory)
  - 「関数形式マクロの代わりに関数を使うべきである」
- **理由 (Rationale)**:
  - ログ、BLE データ、時間、Devicetree の API は、Zephyr がマクロで提供しています。ログは、モジュール名やログレベルの情報を、コンパイル時に処理する必要があります。
- **安全対策 (Mitigation)**:
  - アプリ独自の関数形式マクロは、原則として、定義しません。独自の定数は、`#define` の名前付き定数 (引数なし) にします。
  - 唯一の例外は、`app/thermo-gateway/src/ble.c` の `adv_hexdump()` です。デバッグログが有効なときは `static` 関数で、無効なときは、何もしないマクロ (`do {} while (0)`) になります。マクロの引数は、展開しません。

### 例外 3: マクロ引数の括弧、# / ## 演算子
- **該当ルール**: MISRA C:2012 Rule 20.7 (Required), Rule 20.10 (Advisory), Rule 20.12 (Required)
- **理由 (Rationale)**:
  - `LOG_MODULE_REGISTER` や Devicetree のマクロ (`DT_*`) は、`##` による識別子の連結を使って、モジュールごとの定義を生成します。
- **安全対策 (Mitigation)**:
  - アプリのコードでは、`#` / `##` を使いません。利用は、Zephyr のマクロに限ります。

### 例外 4: オブジェクトポインタの型のキャスト
- **該当ルール**: MISRA C:2012 Rule 11.3 (Required)
  - 「オブジェクトへのポインタを、別の型のオブジェクトへのポインタにキャストしてはならない」
- **理由 (Rationale)**:
  - `BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, ...)` は、マクロの内側で、文字列リテラル (`char`) を `(const uint8_t *)` にキャストします (`app/thermo-node/src/ble.c`)。
  - thermo-gateway では、MQTT ライブラリ (`cloud.c`) と WiFi の API (`wifi_link.c`) が、文字列を `uint8_t` のポインタで受け取ります。また、GATT の通知のデータ (`void *`) は、`sys_get_le16()` に渡すために、`const uint8_t *` にします (`ble.c`)。
- **安全対策 (Mitigation)**:
  - データの長さは、`sizeof(CONFIG_BT_DEVICE_NAME) - 1U` や `strlen()` で決めます。
  - キャストは、API の引数の型に合わせるときだけに限り、`char` の文字列と `uint8_t` のバイト列の間で行います。`const` は、外しません。
### 例外 5: 早期リターン (単一終了点規則の例外)
- **該当ルール**: MISRA C:2012 Rule 15.5 (Advisory)
  - 「関数は末尾に単一の終了点を持つべきである」
- **理由 (Rationale)**:
  - 初期化処理 (`ble_init()`, `sensor_init()` など) では、各 API の失敗ごとに、エラーコードをすぐに呼び出し元へ返します。単一の終了点にすると、エラー状態を保持する変数と、ネストが増え、かえって読みにくくなります。
- **安全対策 (Mitigation)**:
  - エラーを返す前に、`LOG_ERR` で原因をログに出力します。
  - 成功は `EXIT_SUCCESS`、失敗は負の `errno` 値 (`-ENODEV` など) か、API が返したエラーコードにそろえます。
  - アプリのコードは、リソース (メモリ、ファイル) を確保しないので、早期リターンによる解放漏れは起きません。

### 例外 6: 不変な制御式 (無限ループ)
- **該当ルール**: MISRA C:2012 Rule 14.3 (Required)
  - 「制御式は、不変であってはならない」
- **理由 (Rationale)**:
  - `main()` は、電源が入っている間、一定間隔で、温度の読み取りや、稼働状況の出力を続けるファームウェアのメインループです。終了することはありません。
- **安全対策 (Mitigation)**:
  - ループは `while (true)` (`<stdbool.h>`) と明示し、それ以外の場所では、使いません。
  - 無限ループの後ろには、到達しないコード (`return` など) を書きません (Rule 2.1)。
  - 各周回で `k_sleep()` により、他のスレッドに実行を譲ります。

### 例外 7: 1 文の本体に {} を付けない
- **該当ルール**: MISRA C:2012 Rule 15.6 (Required)
  - 「`if` / `else` / `for` / `while` / `do` の本体は、複合文でなければならない」
- **理由 (Rationale)**:
  - 本体が 1 文のとき、`{}` を付けない書き方を許す (`CODING_STYLE.md` の例外 5)。短い条件の処理が、読みやすくなる。
- **安全対策 (Mitigation)**:
  - 本体が 2 文以上のときは、必ず `{}` を付ける。
  - 本体は、次の行に、インデントして書く (1 行に続けない。`AllowShortIfStatementsOnASingleLine: false` と `AllowShortLoopsOnASingleLine: false` で、整形が確認する)。
  - `-Wall` に含まれる `-Wmisleading-indentation` で、インデントと本体の範囲の食い違い (`goto fail` の誤りなど) を検出する。

### 例外 8: ポインタへの整数の加算
- **該当ルール**: MISRA C:2012 Rule 18.4 (Advisory)
  - 「`+`、`-`、`+=`、`-=` 演算子を、ポインタ型の式に適用してはならない」
- **理由 (Rationale)**:
  - GATT の通知のデータには、温度 (2 byte) と湿度 (2 byte) が続けて並んでいます。湿度の位置は、先頭のポインタに、温度のサイズを足して求めます (`app/thermo-gateway/src/ble.c`)。
- **安全対策 (Mitigation)**:
  - 足す前に、通知の長さが `THERMO_TEMPERATURE_SIZE` (4 byte) と等しいことを確認します。足した先から読むのは、2 byte だけです。
  - 足す値は、`sizeof(uint16_t)` の名前付きの式で、マジックナンバーを使いません。

### 例外 9: errno の参照
- **該当ルール**: MISRA C:2012 Rule 22.8, 22.9, 22.10 (Required)
  - 「`errno` を使う関数の呼び出しの前に、`errno` を 0 にし、呼び出しの後に、`errno` を検査しなければならない」
- **理由 (Rationale)**:
  - ソケットの `poll()` は、失敗したことを `-1` で返し、原因を `errno` に入れます。呼び出し元へ、原因 (負の `errno` 値) を伝えるために、参照します (`poll_input()`。`app/thermo-gateway/src/cloud.c`)。
- **安全対策 (Mitigation)**:
  - 戻り値が負のとき (失敗が確定したとき) だけ、`errno` を参照します。直後に、`-errno` を返し、保持しません。
  - ほかの場所では、`errno` を参照しません (`errno.h` は、`EINVAL` などの定数のために含めます)。

---

## 4. 例外としていないもの (参考)

次の機能は、アプリのコードで使わないため、例外に該当しません。使う必要が出たときは、このドキュメントに例外として追加してください。

| 機能 | MISRA C:2012 | 備考 |
|:---|:---|:---|
| `goto` | Rule 15.1 | エラー処理は、早期リターンで行う (リソースを確保しないため) |
| 動的メモリ (`malloc` / `free`) | Rule 21.3 | 静的確保のみ。使う場合は、確保結果を検査し、解放後に `NULL` を代入する (`CODING_STYLE.md`) |
| 再帰呼び出し | Rule 17.2 | |
| 標準入出力 (`<stdio.h>`) | Rule 21.6 | ログは Zephyr Logging API |
| 未定義マクロを `#if` の値として評価 | Rule 20.9 | `CONFIG_SIMULATOR` は `#ifdef` で、Devicetree は `DT_NODE_HAS_PROP` (0 か 1 に展開される) で判定する |
| シグナル (`<signal.h>`) | Rule 21.5 | |
| `exit()` / `atexit()` | Rule 21.8 | `main()` の戻り値で、終了状態を返す |
| 可変長引数 (`<stdarg.h>`) | Rule 17.1 | アプリ独自の可変長引数の関数は、定義しない |
| 浮動小数点数 | Rule 14.1 | 温度と湿度は、10 倍の整数 (0.1 ℃、0.1 % の単位) で扱う |
