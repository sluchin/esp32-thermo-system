# プロジェクト設定

ESP32C3 上で動作する Zephyr RTOS ベースの BLE サーモメータシステム。
温度センサノード (`thermo-node`) とゲートウェイ (`thermo-gateway`) で構成される。

## コマンド

### ビルド (Docker 推奨)
- `docker compose run` には、必ず `--rm` を付ける (終わったコンテナを、残さないため)。
- Thermo Node ビルド: `docker compose run --rm build-thermo-node`
- Thermo Gateway ビルド: `docker compose run --rm build-thermo-gateway`
- 開発シェル: `docker compose run --rm dev`

### ビルド (ローカル West)
- ワークスペース初期化: `west init -l . && west update`
- Thermo Node ビルド: `west build -b xiao_esp32c3 app/thermo-node`
- Thermo Gateway ビルド: `west build -b xiao_esp32c3 app/thermo-gateway`
- クリーンビルド: `west build -p always -b xiao_esp32c3 app/<app-name>`

### フラッシング
- `esptool -p /dev/ttyACM0 write-flash 0x0 build/thermo-gateway/zephyr/zephyr.bin` (Node は `build/thermo-node/...`。`scripts/flash.sh <node|gateway> [PORT]` でもよい)

### シリアルモニター
- `picocom -b 115200 /dev/ttyACM0`

## プロジェクト構成

- `app/`: アプリケーションソース
  - `thermo-node/`: センサノード。ADC で温度を読み取り BLE で配信。
  - `thermo-gateway/`: ゲートウェイ。周辺ノードをスキャンしデータを集約。
- `west.yml`: West マニフェストファイル。
- `docker-compose.yml` / `Dockerfile`: 開発環境定義。
- `.github/workflows/`: GitHub Actions による CI 設定。

## コーディング規約

@../CODING_STYLE.md

## 開発フロー

1. 機能開発は `ccr-*` ブランチで行う。
2. Push 時に GitHub Actions (`.github/workflows/build.yml`) が、開発用の Docker イメージ (ローカルと同じ環境) で、`docker compose` のサービスを実行する。
   - `build` ジョブ: `build-thermo-node` / `build-thermo-gateway` (実機用) と、`*-sim` (native_sim 用)。ビルドに警告が出たら失敗する。成果物 (`zephyr.elf` など) とビルドのログは、アーティファクトとして保存する。`test` / `coverage` / `docs` / `analyze` / `lint` の各ジョブも、実行のログを、アーティファクト (`<サービス名>_log`、`docs_log`。サービス名の `-` は `_` にする) として保存する (失敗したときも保存する)。アーティファクトの名前は、小文字のスネークケースにする (`build_thermo_node` など)。
   - `test` ジョブ: `test-thermo-node` / `test-thermo-gateway` (単体テスト)。
   - `docs` ジョブ: `docs-thermo` (Doxygen のドキュメントを生成する。警告があれば失敗する)。
   - `coverage` ジョブ: `coverage-thermo-node` / `coverage-thermo-gateway` (行と分岐のカバレッジが 100% であること)。
   - `analyze` ジョブ: `analyze-thermo-node` / `analyze-thermo-gateway` (静的解析。指摘があれば失敗する)。
   - `lint` ジョブ: `format-thermo` (整形の確認) / `lint-thermo` (checkpatch.pl) / `whitespace-thermo` (行末の空白の確認)。指摘があれば失敗する。
   - Docker イメージは、レイヤーを GitHub Actions のキャッシュに保存する (`.github/actions/docker-image`)。ビルドコンテキストは `west.yml` だけなので、`Dockerfile` か `west.yml` を変えたときだけ、イメージを作り直す。
3. PR マージ前に、`build` / `test` / `analyze` / `lint` の全ジョブの成功を確認。

### コード修正時のルール

- コードを修正したら、必ず Thermo Node と Thermo Gateway の両方をビルドする (`docker compose run --rm build-thermo-node` / `docker compose run --rm build-thermo-gateway`)。`native_sim` に関わる変更は、`build-thermo-node-sim` / `build-thermo-gateway-sim` も実行する。
  - ビルド出力に警告を出さない。警告やエラーが出たら修正し、出なくなるまで繰り返す。
  - 使用しているライブラリのヘッダが警告を出すときは、そのヘッダの `#include` を `#pragma GCC diagnostic push` / `ignored "-W..."` / `pop` で囲む。ただし、Zephyr のヘッダは、マクロがアプリ側のコードに展開されるため、この方法では抑えきれない (試したところ、970 件が残った)。そのため、`app/warnings.cmake` で、Zephyr の include ディレクトリをシステムヘッダとして扱う。
  - 整形と lint: コードを修正したら、`docker compose run --rm format-thermo-fix` で整形して、`docker compose run --rm lint-thermo` (Zephyr の checkpatch.pl) と、`docker compose run --rm whitespace-thermo` (全てのテキストファイルの行末の空白の確認) を実行する。`docker compose run --rm format-thermo` は、整形が必要なら失敗する (確認だけ)。詳細は、`CODING_STYLE.md`。
  - 単体テスト: `docker compose run --rm test-thermo-node` / `docker compose run --rm test-thermo-gateway` (「テスト」を参照)。コードを修正したら、実行して、全て通ることを確認する。
  - 静的解析: `docker compose run --rm analyze-thermo-node` / `docker compose run --rm analyze-thermo-gateway` (gcc の `-fanalyzer` を、app のソースだけに `-O0` で掛ける。通常のビルドとは分けてあり、指摘があれば失敗する)。コードを修正したら、実行して、指摘が出ないことを確認する。
  - 警告オプションの一覧は `app/warnings.txt` にある (`-Wall` `-Wextra` `-Wpedantic` `-Wconversion` `-Wsign-conversion` `-Wshadow` `-Wformat=2` など約 55 個)。`app/warnings.cmake` が読み込み、アプリのソース (`app/` 配下) だけに付ける。Zephyr のヘッダは、システムヘッダとして扱うので、警告を出さない。オプションを足し引きするときは、`warnings.txt` だけを直す。
  - ビルドを実行できなかったときは、そのことを報告する (成功したとは書かない)。
  - **例外**: 変更が `.claude/CLAUDE.md` や `README.md` などの文書のみの場合は、ビルドしない。
- ビルドやコマンドの手順を変更したら、`README.md` / `SETUP.md` / `SIMULATION.md` も更新する。
- ソースファイルを追加したら、該当アプリの `CMakeLists.txt` も更新する。

### タスク完了時の動作

- 修正が完了し、ビルドが成功したら、`git diff --stat` で差分を確認し、変更点と理由の簡潔なまとめをユーザーに報告する。
- ユーザーに依頼されない限り、`git commit` / `git push` は行わない。

## テスト

単体テストは、Zephyr のテストフレームワーク Ztest (`zephyr/ztest.h`) と、モックの FFF (`zephyr/fff.h`) で書く。`native_sim` (ホスト上) で実行し、Zephyr のテストランナー twister が実行する。

- カバレッジ: `docker compose run --rm coverage-thermo-node` / `docker compose run --rm coverage-thermo-gateway` (`app/thermo-*/src` の行と分岐が 100% でなければ失敗する。分岐は `LOG_*` と `LOG_HEXDUMP_*` マクロの内部を除く)。テストを追加・変更したら、実行して、100% を保つ。
- 実行: `docker compose run --rm test-thermo-node` / `docker compose run --rm test-thermo-gateway` (全てのテストを実行する。結果は、コンテナの `/tmp` に出力する)
- 配置: `app/<アプリ>/tests/<対象>/`。1 ディレクトリが 1 つのテストアプリ (`CMakeLists.txt` `prj.conf` `testcase.yaml` `src/test_*.c`)。twister が、`testcase.yaml` を探して、自動で実行する。
  - `sensor` (thermo-node): `sensor.c`。ADC エミュレータ (`zephyr,adc-emul`。入力電圧とエラーを、テストから設定する) を使う実機用の経路と、シミュレーション値の経路の、2 つの構成 (`TEST_MODE=adc` / `simulator`)。
  - `ble` (両方): `ble.c`。Bluetooth スタックは使わず、`bt_enable()` `bt_le_adv_start()` `bt_le_scan_start()` を、FFF のモックにして、引数と、エラーの伝わり方を確認する。
  - `switchbot` (thermo-gateway): `switchbot.c`。SwitchBot 屋外用温湿度計のアドバタイズの解析 (サービスデータと製造者データ) と、機器ごとの記録と、送信の間引き (間隔、カウンタの一周、機器の数の上限)。Zephyr の関数を呼ばないので、モックは使わない。
  - `payload` / `cfg` / `shell` / `wifi` / `cloud` (thermo-gateway): AWS IoT Core への送信 (`payload.c` `cfg.c` `cfg_shell.c` `wifi_link.c` `cloud.c`)。settings、TLS の認証情報、net_mgmt、MQTT ライブラリ、ソケットは、FFF のモックにして、接続の手順と各段階の失敗、再試行の間隔、設定の保存と読み込みを確認する。シェルは、ダミーのバックエンドで実行する。`cloud` は、`cloud_step()` を直接呼ぶ。
  - `main` (両方): `main.c`。`ble` と `sensor` (gateway は、`ble` と `cloud`) の関数を、FFF のモックにして、初期化に失敗したときに `EXIT_FAILURE` を返すことと、メインループ (別スレッドで動かす) が動き続けることを確認する。`main.c` の `main()` は、テストの `main()` と名前が同じなので、`CMakeLists.txt` で名前を変える。
- 規約:
  - テストにも、`app/warnings.txt` の警告オプションを付ける (`include(.../warnings.cmake)`)。モックが、関数を再宣言するため、`-Wredundant-decls` だけは外している。
  - ソースを足したり、関数を変えたりしたら、対応するテストを足す (正常系と、エラーの伝わり方)。
  - テストの名前は、`test_<対象の関数名>_<条件>` (`test_init_failure` など)。
  - 実機 (ESP32C3) での動作確認は、別に必要 (ADC の実際の値、BLE の通信、WiFi と AWS IoT Core との TLS の接続は、単体テストでは確認できない)。

## コミットガイドライン

- コミットメッセージは英語で記述
- メッセージ本体はダッシュ/ハイフンで始まる（例: `- Fix bug in parser`）
- コミットメッセージに `Co-Authored-By` または Claude 帰属行を含めない
- **rebase したあとの push は、`git push --force-with-lease --force-if-includes origin <ブランチ名>` で行う** (`--force` は使わない。リモートに、手元に取り込んでいないコミットがあれば、push は失敗する)。ユーザーに依頼されたときだけ、push する。
