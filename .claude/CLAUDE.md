# プロジェクト設定

ESP32C3 上で動作する Zephyr RTOS ベースの BLE サーモメータシステム。
温度センサノード (`thermo-node`) とゲートウェイ (`thermo-gateway`) で構成される。

## コマンド

### ビルド (Docker 推奨)
- `docker compose run` には、必ず `--rm` を付ける (終わったコンテナを、残さないため)。
- Thermo Node ビルド: `docker compose run --rm build-node` (OLED と RTC を外すときは、`THERMO_DISPLAY=n` と `THERMO_RTC=n` を付ける。`SETUP.md` の「OLED の表示と時刻」)
- Thermo Gateway ビルド: `docker compose run --rm build-gateway`
- 両方まとめて: `docker compose run --rm build` / `test` / `coverage` / `analyze` (node と gateway の両方を、順に実行する。個別のサービスは、`build-node` `build-gateway` `test-node` `coverage-gateway` のように、`-node` か `-gateway` を付ける。`format` `format-fix` `lint` `whitespace` `docs` は、両方のアプリを対象にする (名前に、アプリは付かない)。CI は、個別のサービスを使う。`scripts/build.sh` と `scripts/analyze.sh` が本体)
- 開発シェル: `docker compose run --rm dev`
- クリーン: `docker compose run --rm clean` (`build/` と `docs/` を消す。ビルドの不具合を疑うときに、最初からやり直す)

### ビルド (ローカル West)
- ワークスペース初期化: `west init -l . && west update`
- Thermo Node ビルド: `west build -b xiao_esp32c3 app/thermo-node`
- Thermo Gateway ビルド: `west build -b xiao_esp32c3 app/thermo-gateway`
- クリーンビルド: `west build -p always -b xiao_esp32c3 app/<app-name>`

### フラッシング
- `esptool -p /dev/ttyACM0 write-flash 0x0 build/thermo-gateway/zephyr/zephyr.bin` (Node は `build/thermo-node/...`。`scripts/flash.sh <node|gateway> [PORT]` でもよい)

### シリアルモニター
- `picocom -b 115200 /dev/ttyACM0`
- 起動のログ (リセットのたびに USB が切れる) を見るとき: `while true; do [ -c /dev/ttyACM0 ] && stty -F /dev/ttyACM0 115200 raw -echo && cat /dev/ttyACM0; sleep 0.3; done` (読み取りだけ。`picocom` のループは、端末の応答がシェルに返り続けて、出力が乱れることがある。`SETUP.md` の「起動のログを見る」)
- Thermo ノードのシェルには、ファイルシステムのコマンド `fs` がある (`CONFIG_FILE_SYSTEM_SHELL=y`。`SETUP.md` の「ファイルシステムのシェル」)。`fs rm` と `fs write` も使えるので、ログの書き込み中には、実行しない。

### クラウドのセッション (claude.ai/code) で Docker を使うとき

**通常の `docker compose build` は、使わない** (curl の終了コード 60 で失敗する。理由は、下)。イメージは `scripts/cloud-docker.sh build` で作り、サービスは `scripts/cloud-docker.sh run <サービス>` で実行する (イメージがなければ、先に `build` する)。環境変数 `CLAUDE_CODE_REMOTE` が `true` なら、クラウド。

```bash
scripts/cloud-docker.sh build                  # 開発用のイメージ (esp32-thermo-system) を作る (プロキシと CA の設定を足す)
scripts/cloud-docker.sh run build-node  # docker compose run --rm build-node (test-node なども同じ)
scripts/cloud-docker.sh start                  # Docker のデーモンを起動する (起動済みなら、何もしない)
```

- クラウドの VM には、Docker (`dockerd`) が入っているが、デーモンは、自動では起動しない。SessionStart フック (`.claude/settings.json`) が、セッションの開始のときに、`scripts/cloud-docker.sh hook` で起動する (クラウドのときだけ)。起動していないときは、`scripts/cloud-docker.sh start`。ログは、`/tmp/dockerd.log`。
- VM の外への通信は、全て、Anthropic のプロキシを通る。通信できるドメインは、環境の「Network access」で決まる (標準の Trusted の一覧にないドメインは、「Custom」で足す)。
- **curl の終了コード 60 の原因** : VM の通信は、プロキシと、そのプロキシの CA 証明書を使うが、**コンテナは、どちらも引き継がない**。
  - VM には、`HTTPS_PROXY=http://127.0.0.1:<ポート>` が設定されている。**ポートは、セッションの途中でも変わる** (例: 37451、45999) ので、固定で書かず、実行のたびに `$HTTPS_PROXY` から読む。`127.0.0.1` は、コンテナの中では、コンテナ自身を指す。
  - CA 証明書は、`/root/.ccr/ca-bundle.crt` にある (`SSL_CERT_FILE` などが指す)。VM のシステムの信頼ストアには、入っていない。
- **`cloud-docker.sh` がしていること**: `Dockerfile` を、一時的にコピーして (リポジトリは、変えない)、CA を、**証明書ごとに 1 ファイルに分けて**、信頼ストアに入れる行を足す。`--network host` を付け、プロキシを、`ARG` で渡す (`ENV` には、焼き込まない)。Python (`pip`、`west`) は、システムの信頼ストアを見ないので、`PIP_CERT`、`REQUESTS_CA_BUNDLE`、`SSL_CERT_FILE` に、`/etc/ssl/certs/ca-certificates.crt` を教える。**プロキシは、HTTPS (`curl`、`pip`、`west` の git) だけに渡し、`HTTP_PROXY` は渡さない** (`apt` は、Ubuntu のミラーに、HTTP で接続するので、`HTTP_PROXY` を渡すと、プロキシに拒否される)。`apt` が、それでも失敗するときは、環境の「Allowed domains」に、`archive.ubuntu.com` と `security.ubuntu.com` を足す。ビルドのコンテキストは、`west.yml` だけ (`Dockerfile` の `COPY` が使う)。
- **Docker Hub の `429`** (ベースのイメージ `ubuntu:24.04` の取得): 失敗したときは、Google のミラー `mirror.gcr.io/library/ubuntu:24.04` から取って、`ubuntu:24.04` のタグを付けてから、ビルドする (スクリプトが、自動でする。環境の「Allowed domains」に、`mirror.gcr.io` が要るかもしれない)。
- イメージに入った CA は、セッションが変わって、プロキシの CA が変わったときは、古くなる。TLS のエラーが出たら、`scripts/cloud-docker.sh build` で、作り直す。
- **未確認** (クラウドでの実行): イメージの作成の完走 (イメージは、約 25 GB で、ビルドに、時間と、ディスクが、かかる。クラウドの VM の余裕は、未確認)。Zephyr SDK のダウンロード (`github.com` のリリース。ダウンロード先が、別のドメイン (`objects.githubusercontent.com`、`release-assets.githubusercontent.com` など) に、リダイレクトされ、標準の許可リストにないときは、「Allowed domains」に足す)。`west update` (GitHub の git)。`west blobs fetch`。`pip install` (`pypi.org` は、標準の許可リストにある)。
- 同じ失敗を、繰り返さない。

## プロジェクト構成

- `app/`: アプリケーションソース
  - `thermo-node/`: センサノード。DHT11 (または ADC) で温度と湿度を読み取り BLE で配信し、拡張ボードの OLED に、温度、湿度、日時を表示する (日時は、拡張ボードの RTC。PCF8563 のドライバは、`drivers/rtc_pcf8563.c`)。
  - `thermo-gateway/`: ゲートウェイ。周辺ノードをスキャンしデータを集約。
  - `common/`: 2 つのアプリで共有するコード。BLE の UUID (`thermo_ble_uuid.h`)、ログのレベル (`thermo_log.h`)、設定とシェル (`cfg.c` `cfg_shell.c`。SSID、パスワード、エンドポイント、クライアント ID、証明書)、WiFi (`wifi_link.c`)、SNTP (`ntp.c`)、AWS IoT Core への MQTT (`cloud.c`)、ペイロードの作成 (`payload.c`)、SwitchBot の値 (`switchbot_sample.h`)。ゲートウェイは、起動のときから、動かし続ける。ノードは、ゲートウェイにつながらないときだけ、`cloud_set_enabled()` で動かす (`thermo-node/src/direct.c`)。ノードは、`app/thermo-node/src/shell_extra.h` で、`thermo` コマンドに `sync` を足す。
- `man/`: このプロジェクトが使っている Zephyr の API の日本語訳 (サブシステムごとの Markdown。`man/index.md` から読む)。新しい Zephyr の API を使い始めたら、該当のファイルに足す。`docs/` は生成物で、ビルドのたびに消えるので、ここには置かない。
- `west.yml`: West マニフェストファイル。
- `docker-compose.yml` / `Dockerfile`: 開発環境定義。
- `.github/workflows/`: GitHub Actions による CI 設定。

## 起動画面と起動音の素材

素材 (絵、題名、メロディ) は `app/thermo-node/src/boot_assets.c` (オリジナル)。ボタンを押したときの音は `app/thermo-node/src/button_assets.c` (オリジナル)。`app/thermo-node/private/src/boot_assets.c` と `app/thermo-node/private/src/button_assets.c` があれば、それぞれ、それを使う (別のプライベートリポジトリを、サブモジュールとして置く。`SETUP.md` の「起動画面と起動音」)。

- **既存のゲームなどの、著作権や商標のある素材 (絵、曲) を、このリポジトリ (公開) に、コミットしない。** 履歴にも残さない。素材のデータは、ユーザーが、プライベートリポジトリに、用意する (こちらで、そのままの音階や絵を、書き写さない)。
- 素材を入れてビルドした実行ファイル (`zephyr.elf`、`zephyr.bin`) を、公開しない (CI の成果物、リリース)。CI は、サブモジュールを取り込まない。
- 差し替えるファイルは、`boot_assets.h` と `button_assets.h` の宣言を、全て、同じ名前と形で定義する。素材がなくても、ビルドできること。

## コーディング規約

@../CODING_STYLE.md

## 開発フロー

1. 機能開発は `ccr-*` ブランチで行う。
2. Push 時に GitHub Actions (`.github/workflows/build.yml`) が、開発用の Docker イメージ (ローカルと同じ環境) で、`docker compose` のサービスを実行する。
   - `build` ジョブ: `build-node` / `build-gateway` (実機用) と、`*-sim` (native_sim 用)。ビルドに警告が出たら失敗する。成果物 (`zephyr.elf` など) とビルドのログは、アーティファクトとして保存する。`test` / `coverage` / `docs` / `analyze` / `lint` の各ジョブも、実行のログを、アーティファクト (`<サービス名>_log`、`docs_log`。サービス名の `-` は `_` にする) として保存する (失敗したときも保存する)。アーティファクトの名前は、小文字のスネークケースにする (`build_node` など)。
   - `test` ジョブ: `test-node` / `test-gateway` (単体テスト)。
   - `docs` ジョブ: `docs` (Doxygen のドキュメントを生成する。警告があれば失敗する)。
   - `coverage` ジョブ: `coverage-node` / `coverage-gateway` (行と分岐のカバレッジが 100% であること)。
   - `analyze` ジョブ: `analyze-node` / `analyze-gateway` (静的解析。指摘があれば失敗する)。
   - `lint` ジョブ: `format` (整形の確認) / `lint` (checkpatch.pl) / `whitespace` (行末の空白の確認)。指摘があれば失敗する。
   - `cleanup` ジョブ: 3 日より前の実行 (ログと artifact) を、push のたびに削除する (`gh api` で、実行を削除する。権限は `actions: write`)。artifact は、`retention-days: 3` で、3 日で期限が切れる。保存する期間を変えるときは、`build.yml` の `KEEP_DAYS` と、全ての `retention-days` を、そろえて変える。
   - Docker イメージは、レイヤーを GitHub Actions のキャッシュに保存する (`.github/actions/docker-image`)。ビルドコンテキストは `west.yml` だけなので、`Dockerfile` か `west.yml` を変えたときだけ、イメージを作り直す。
3. PR マージ前に、`build` / `test` / `analyze` / `lint` の全ジョブの成功を確認。

### コード修正時のルール

- コードを修正したら、必ず Thermo Node と Thermo Gateway の両方をビルドする (`docker compose run --rm build-node` / `docker compose run --rm build-gateway`)。`native_sim` に関わる変更は、`build-node-sim` / `build-gateway-sim` も実行する。
  - Devicetree (`*.overlay`) と Kconfig のインデントは、空白 4 つ (Zephyr のタブ 8 ではない。`CODING_STYLE.md` の例外 1。Kconfig の `help` の本文は、`help` より、空白 2 つ深くする)。新しい overlay と Kconfig も、タブを使わない。
  - ビルド出力に警告を出さない。警告やエラーが出たら修正し、出なくなるまで繰り返す。
  - 使用しているライブラリのヘッダが警告を出すときは、そのヘッダの `#include` を `#pragma GCC diagnostic push` / `ignored "-W..."` / `pop` で囲む。ただし、Zephyr のヘッダは、マクロがアプリ側のコードに展開されるため、この方法では抑えきれない (試したところ、970 件が残った)。そのため、`app/warnings.cmake` で、Zephyr の include ディレクトリをシステムヘッダとして扱う。
  - 整形と lint: コードを修正したら、`docker compose run --rm format-fix` で整形して、`docker compose run --rm lint` (Zephyr の checkpatch.pl) と、`docker compose run --rm whitespace` (全てのテキストファイルの行末の空白の確認) を実行する。`docker compose run --rm format` は、整形が必要なら失敗する (確認だけ)。詳細は、`CODING_STYLE.md`。
  - 単体テスト: `docker compose run --rm test-node` / `docker compose run --rm test-gateway` (「テスト」を参照)。コードを修正したら、実行して、全て通ることを確認する。
  - 静的解析: `docker compose run --rm analyze-node` / `docker compose run --rm analyze-gateway` (gcc の `-fanalyzer` を、app のソースだけに `-O0` で掛ける。通常のビルドとは分けてあり、指摘があれば失敗する)。コードを修正したら、実行して、指摘が出ないことを確認する。
  - 警告オプションの一覧は `app/warnings.txt` にある (`-Wall` `-Wextra` `-Wpedantic` `-Wconversion` `-Wsign-conversion` `-Wshadow` `-Wformat=2` など約 55 個)。`app/warnings.cmake` が読み込み、アプリのソース (`app/` 配下) だけに付ける。Zephyr のヘッダは、システムヘッダとして扱うので、警告を出さない。オプションを足し引きするときは、`warnings.txt` だけを直す。
  - ビルドを実行できなかったときは、そのことを報告する (成功したとは書かない)。
  - **例外**: 変更が `.claude/CLAUDE.md` や `README.md` などの文書のみの場合は、ビルドしない。
- ビルドやコマンドの手順を変更したら、`README.md` / `SETUP.md` / `SIMULATION.md` も更新する。
- **Markdown の見出し (`#` で始まる行) の中に、バッククォート (インラインコード) を使わない**: Doxygen が、HTML の見出しに、`<tt>` と `</tt>` を、文字のまま表示するため (GitHub Pages に公開する文書が、崩れる)。見出しは、プレーンテキストで書く。本文の中では、使ってよい。
- ソースファイルを追加したら、該当アプリの `CMakeLists.txt` も更新する。

### タスク完了時の動作

- 修正が完了し、ビルドが成功したら、`git diff --stat` で差分を確認し、変更点と理由の簡潔なまとめをユーザーに報告する。
- ユーザーに依頼されない限り、`git commit` / `git push` は行わない。

## テスト

単体テストは、Zephyr のテストフレームワーク Ztest (`zephyr/ztest.h`) と、モックの FFF (`zephyr/fff.h`) で書く。`native_sim` (ホスト上) で実行し、Zephyr のテストランナー twister が実行する。

- カバレッジ: `docker compose run --rm coverage-node` / `docker compose run --rm coverage-gateway` (`app/thermo-*/src` と `app/thermo-node/drivers` と `app/common` の行と分岐が 100% でなければ失敗する。`app/common` は、それを、ビルドに含めるテスト (今は、gateway のテスト) で、集計する。分岐は `LOG_*` と `LOG_HEXDUMP_*` マクロの内部を除く)。テストを追加・変更したら、実行して、100% を保つ。
- 実行: `docker compose run --rm test-node` / `docker compose run --rm test-gateway` (全てのテストを実行する。結果は、コンテナの `/tmp` に出力する)
- 配置: `app/<アプリ>/tests/<対象>/`。1 ディレクトリが 1 つのテストアプリ (`CMakeLists.txt` `prj.conf` `testcase.yaml` `src/test_*.c`)。twister が、`testcase.yaml` を探して、自動で実行する。
  - `sensor` (thermo-node): `sensor.c`。5 つの構成 (`TEST_MODE`): `sensor_api` (偽のセンサ `vnd,test-sensor`。DHT11 などの、センサ API の経路。値とエラーを、テストから設定する)、`adc` (ADC エミュレータ `zephyr,adc-emul`。入力電圧とエラーを、テストから設定する)、`adc_setup_error` / `adc_convert_error` (ADC の設定と換算の失敗)、`simulator` (シミュレーション値)。
  - `oled` (thermo-node): `oled.c`。表示装置は、偽のもの (`vnd,test-display`) に置き換えて、CFB (文字のフレームバッファ) の関数は、FFF のモックにする。初期化の各段階の失敗と、点灯と消灯 (`oled_set_power()`。blanking の開始と停止と、失敗) と、描く文字列と位置 (負の温度、測定値なし、湿度なし、日時あり、日時が未設定) と、グラフのページ (最小と最大、折れ線の座標、履歴なし、点が 1 つ、測定値のない点を描かないこと、縦軸の余白、描画の失敗)、起動画面 (題名の位置、ドット、絵の位置、はみ出しの切り取り、口の開き方、フレームの範囲、描画の失敗) を確認する。`CONFIG_DISPLAY=y` にすると、native_sim の SDL の表示も有効になり、終了時に落ちることがあるので、`CONFIG_SDL_DISPLAY=n` にしている。
  - `buzzer` (thermo-node): `buzzer.c`。PWM のコントローラを、偽のもの (`vnd,test-pwm`) に置き換える。メロディの順序、周波数から周期への換算 (デューティ比 50%)、休符、再生の中断と再開、PWM の失敗でメロディを止めること。システムのワークキューで鳴らすので、仮想の時間を進めて確認する。
  - `history` (thermo-node): `history.c`。測定値の区切りごとの平均、リングバッファ (満杯で古い点を上書き)、古い順の取り出し (今の区切りの平均を含む)、取り出す数の制限、負の平均の切り捨て、測定値のない区切り (`HISTORY_NONE`。間の数の上限)、保存してあった点の復元 (`history_restore()`)。関数だけなので、モックはない。
  - `button` (thermo-node): `button.c`。GPIO のコントローラを、偽のもの (`vnd,test-gpio`) に置き換える。初期化の失敗と、押された動作の検出 (押し続けても 1 回、起動のときに押されていても数えない)。
  - `node_time` (thermo-node): `node_time.c`。RTC のデバイスを、偽のもの (`vnd,test-rtc`。時刻 (`rtc_time`) と、読み書きの結果を、テストから設定する) に置き換える。UTC からローカルタイムへの変換 (日付と年の繰り上がり、うるう日)、UNIX 時刻から `rtc_time` (月は 0 から 11、年は 1900 年からの年数、曜日) への換算、時刻が未設定 (`-ENODATA`)、範囲外の時刻の設定、RTC の失敗を確認する。
  - `rtc_pcf8563` (thermo-node): `drivers/rtc_pcf8563.c` (PCF8563 の RTC ドライバ。compatible は `thermo,pcf8563`)。I2C のコントローラを、偽のもの (`vnd,test-i2c`) に置き換えて、PCF8563 のレジスタ (BCD) を模擬する。レジスタと `rtc_time` の換算 (12 月、2099 年、世紀のビットの無視、曜日)、時刻が未設定 (電圧低下のビット)、範囲外のレジスタの値、時刻の設定 (BCD、範囲外は `-EINVAL`)、通信の失敗、初期化 (I2C のバスが使えない) を確認する。
  - `wifi_time` / `wifi_shell` (thermo-node): BLE で時刻が届かないときの、WiFi と SNTP による時刻の取得 (`wifi_time.c` `wifi_shell.c`)。cfg、wifi_link (どちらも `app/common/`。モック)、SNTP、ソケットは、FFF のモック。接続、時刻の取得、切断、各段階の失敗 (失敗しても WiFi を切る)、シェル (`thermo sync` と `thermo sd`。`direct_sd_check()` はモック。設定のコマンドは、`app/common/cfg_shell.c` で、gateway のテストが確認する) を確認する。
  - `direct` (thermo-node): `direct.c` (ゲートウェイにつながらないときの、WiFi での直接の送信と、SD カードへの保存、あとからの送信)。ble、cfg、cloud、backlog、node_time の関数は、FFF のモック (SD カードは、メモリ上の配列)。BLE の失敗が 5 回続いたら始めること、つながったら止めること、あきらめたらやり直さないこと、全ての測定値を SD カードに保存すること (ゲートウェイにつながっていれば送信済み、つながっていなければ未送信。SD カードや時刻がないときは保存しない。保存できないときは、そのまま送る)、未送信の記録を古い順にキューに入れること (キューを満杯にしない)、送れた分だけ送信済みにすること、失敗しても続けることを確認する。`direct_sd_check()` (シェルの `thermo sd`) は、使えなければマウントをやり直すこと、使えていればマウントし直さないことを確認する。
  - `backlog` / `backlog_nodisk` / `backlog_fserr` (thermo-node): `backlog.c` (SD カードに保存する、送信済みフラグつきの測定値の記録。上限の件数の輪)。`backlog` は、native_sim の RAM ディスク (`zephyr,ram-disk`。ディスク名 RAM) の FatFS (本物のファイルシステム) を、`/RAM:` にマウントして、追記と古い順の読み出し、送信済みの記録を飛ばすこと、送り終えたことの記録 (フラグの書き換え) と再起動のあとの続き、上限での上書き (送信済みを先に消すこと、再起動のあとも順序と未送信の数が合うこと)、壊れた記録 (CRC)、書きかけの記録、時刻以降の記録の読み出し、範囲の外の時刻を確認する。`backlog_nodisk` は、マウントできない (ディスクの名前が合わない) ときを確認する。`backlog_fserr` は、`fs_*` を FFF のモックにして、起動、追記、読み出し、送り終えたことの記録、時刻以降の記録の読み出しの各段階の失敗 (開く、書く、閉じる、移動、読み出し) を確認する。SD カードそのもの (SPI の配線、フォーマット、抜き差し) は、実機で確認する。
  - `history_store` (thermo-node): `history_store.c` (グラフの履歴の、起動のときの復元)。`node_time_unix()` と `backlog_foreach_since()` は FFF のモック。RTC に時刻が入るまで何もせず、入ったら 1 回だけ復元すること、SD カードの記録が区切りごとの平均になって時刻のとおりの位置に戻ること (記録のなかった区切りは測定値なし)、今の区切り以降の記録を戻さないこと、湿度なしの記録、履歴にすでに点があるとき戻さないこと、読み出しに失敗しても続けることを確認する。
  - `sdlog` (thermo-node): `sdlog.c` (起動の 1 分間の全てのログと、エラーのログを、SD カードにテキストで保存する、ログのバックエンド)。RAM ディスクの FatFS と、本物のログ (`LOG_INF` `LOG_ERR`) で確認する。状態をテストの間で引き継ぐので、`test_1` から `test_7` の名前の順に実行する前提。`sdlog_start()` の前は書かずに溜めること、始めたら溜めたログを書くこと (`boot.log` は作り直して追記、`error.log` はエラーだけ)、溜め切れなかったログをエラーで知らせること、アンマウントや容量がなくて書けないときは止めて `sdlog_start()` で再開すること、起動の 1 分のあとは `boot.log` に書かないこと (エラーは書く)、`error.log` が上限に達したら `error.old` に移すこと。
  - `main_display_timeout` (thermo-node): `main.c` の OLED の自動消灯 (`CONFIG_THERMO_DISPLAY_TIMEOUT_SEC=10`。`tests/main` は、`0` で、消さない)。ボタンを押さずに 10 秒たつと消えること (消えている間は、表示しない)、消えている間にボタンを押すと、点灯するだけで、ページを切り替えないこと、点灯している間にボタンを押すと、ページを切り替えて、消すまでの時間を数え直すこと、消灯と点灯の失敗のやり直し、OLED の初期化に失敗している間は、消灯も点灯もしないこと、初期化のやり直しで使えるようになったら、点灯して、時間を数えること。RTC と WiFi と起動画面とブザーは、無効にして、OLED とボタンだけを確かめる。
  - `ble` (両方): `ble.c`。Bluetooth スタックは使わず、`bt_enable()` `bt_le_adv_start()` `bt_le_scan_start()` を、FFF のモックにして、引数と、エラーの伝わり方を確認する。時刻の同期も含む: ノードは、時刻の特性の書き込み (長さ、位置、コールバックの結果の ATT エラー)、ゲートウェイは、時刻の特性の探索と書き込み (SNTP の同期待ち、1 時間ごとの書き直し、拒否と失敗のあとのやり直し、切断で止まること。仮想時間で進める)。
  - `switchbot` (thermo-gateway): `switchbot.c`。SwitchBot 屋外用温湿度計のアドバタイズの解析 (サービスデータと製造者データ) と、機器ごとの記録と、送信の間引き (間隔、カウンタの一周、機器の数の上限)。Zephyr の関数を呼ばないので、モックは使わない。
  - `payload` / `cfg` / `shell` / `wifi` / `cloud` / `ntp` (thermo-gateway): AWS IoT Core への送信 (`app/common/` の `payload.c` `cfg.c` `cfg_shell.c` `wifi_link.c` `cloud.c` `ntp.c`。ノードと共有するコードも、ここで確認する。`cloud` は、止める (`cloud_set_enabled(false)`)、あきらめる (`cloud_set_attempt_limit()`)、測った時刻つきの送信 (`cloud_publish_temperature_at()`) も確認する)。settings、TLS の認証情報、net_mgmt、MQTT ライブラリ、ソケットは、FFF のモックにして、接続の手順と各段階の失敗、再試行の間隔、設定の保存と読み込みを確認する。シェルは、ダミーのバックエンドで実行する。`cloud` は、`cloud_step()` を直接呼ぶ。
  - `main` (両方): `main.c`。`ble` と `sensor` (node は、`node_time` と `oled` も。gateway は、`ble` と `cloud`) の関数を、FFF のモックにして、初期化に失敗したときに `EXIT_FAILURE` を返すことと、メインループ (別スレッドで動かす) が動き続けることを確認する (node は、OLED の表示と時刻が、1 秒ごとに更新されることと、ボタンでページが順に切り替わること (最後の次は最初)、ボタンを押すとブザーでボタンの音を鳴らすこと (ブザーが使えないとき、鳴らせなかったときも、ページは切り替わる)、グラフの履歴に測定値を加えること、OLED と RTC とボタンの初期化や表示に失敗しても、続けること。起動の 1 分後から 1 分ごとに RTC の時刻を確かめて、なければ WiFi で時刻を取って RTC に設定すること、時刻があれば取らないこと、失敗したら次の確認でやり直して 5 回でやめること)。`main.c` の `main()` は、テストの `main()` と名前が同じなので、`CMakeLists.txt` で名前を変える。
- 規約:
  - テストにも、`app/warnings.txt` の警告オプションを付ける (`include(.../warnings.cmake)`)。モックが、関数を再宣言するため、`-Wredundant-decls` だけは外している。
  - ソースを足したり、関数を変えたりしたら、対応するテストを足す (正常系と、エラーの伝わり方)。
  - テストの名前は、`test_<対象の関数名>_<条件>` (`test_init_failure` など)。
  - 実機 (ESP32C3) での動作確認は、別に必要 (DHT11 の実際の値、BLE の通信、WiFi と AWS IoT Core との TLS の接続は、単体テストでは確認できない)。

## コミットガイドライン

- コミットメッセージは英語で記述
- **件名は、Conventional Commits の形式で書く**: `type(scope): description`。scope (括弧を含む) は、省略できる。description は、英語で、命令形 (例: `fix(thermo-gateway): Fix the TLS handshake timeout`)。
  - type は、次のどれか。
    - `feat`: 機能の追加
    - `fix`: バグの修正
    - `docs`: 文書とコメントだけの変更 (`*.md`、`Doxyfile`、`assets/`、Doxygen コメント)
    - `style`: 整形、コーディング規約の適用 (動作を変えない)
    - `refactor`: 動作を変えない、書き換え
    - `test`: テストだけの変更
    - `build`: ビルドと、開発環境 (`CMakeLists.txt`、`Dockerfile`、`docker-compose.yml`、`west.yml`) の変更
    - `ci`: CI (`.github/`、CI で実行する確認のスクリプト) の変更
    - `chore`: そのほかの雑務 (最初のコミット、 `.gitignore` など)
  - scope は、変更の場所: `thermo-gateway` / `thermo-node` (そのアプリだけ)、`app` (両方のアプリ、共通のコード)、`docker`、`scripts`。
  - 互換性を壊す変更は、type (または scope) の直後に、感嘆符を付ける (Conventional Commits の仕様のとおり)。
  - 複数の type にまたがるときは、変更の中心になる type にする。
- 件名のあとに空行を 1 行入れて、本文を書く。本文はダッシュ/ハイフンで始まる箇条書き（例: `- Fix bug in parser`）
- コミットメッセージに `Co-Authored-By` または Claude 帰属行を含めない
- **rebase したあとの push は、`git push --force-with-lease --force-if-includes origin <ブランチ名>` で行う** (`--force` は使わない。リモートに、手元に取り込んでいないコミットがあれば、push は失敗する)。ユーザーに依頼されたときだけ、push する。
