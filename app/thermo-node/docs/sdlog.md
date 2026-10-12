# sdlog.c の解析

対象: `app/thermo-node/src/sdlog.c` (330 行) と `sdlog.h` (51 行)

Zephyr のログを、SD カードのテキストファイルに保存するための **ログのバックエンド** です。USB のシリアルは、リセットのたびに切れるので、起動のログが取りにくい、という問題を、SD カードで補います。

| ファイル | 内容 |
|:---|:---|
| `boot.log` | 起動から `CONFIG_THERMO_SDLOG_BOOT_SEC` 秒 (既定 60 秒) の間の、全てのレベルのログ。起動のたびに作り直す |
| `error.log` | エラー (`LOG_ERR`) のログ。起動のあとも追記する。`CONFIG_THERMO_SDLOG_ERROR_MAX_BYTES` (既定 64 KB) に達したら `error.old` に移して、新しい `error.log` に書く |

`CONFIG_THERMO_SDLOG=n` のときは、ビルドされません (ヘッダの空のインライン関数が代わりになります)。SD カードのマウントは `backlog.c` が行うので、`CONFIG_THERMO_BACKLOG` が要ります。

## 設計の要点

ログは、どのスレッドからも、割り込みの中からも出ます (`prj.conf` は `CONFIG_LOG_MODE_IMMEDIATE=y` なので、バックエンドの `process()` は、ログを出した文脈で、そのまま呼ばれます)。SD カードへの書き込みは、遅く、割り込みの中では使えないので、2 段に分けています。

```text
ログを出す (LOG_INF など。任意のスレッド、割り込み)
   │  backend_process()
   ▼
文字列にして、RAM のリングバッファに入れる ← 速い。ファイルを触らない
   │  boot_ring (768 バイト) / error_ring (256 バイト)
   ▼
sdlog_flush() ← メインスレッドが 1 秒ごとに呼ぶ。ここだけが SD カードに書く
   ▼
boot.log / error.log
```

SD カードが使えるようになる前 (`sdlog_start()` の前) のログは、RAM のリングバッファに溜まります。マウントが終わると、`backlog_init()` が `sdlog_start()` を呼び、次の `sdlog_flush()` から書き始めます。

## 公開する関数

| 関数 | 役割 |
|:---|:---|
| `sdlog_start()` | SD カードが使えるようになったことを知らせる (`ready = true`) |
| `sdlog_flush()` | 溜めたログをファイルに書く。メインスレッドが、1 秒ごとに呼ぶ |
| `sdlog_panic()` | ログのバックエンドの `panic` 関数。何もしない (パニックのときに、ファイルシステムを触らない) |

## バックエンドの登録

```c
LOG_BACKEND_DEFINE(sdlog_backend, sdlog_api, true);   /* true: 自動で始める */
```

`sdlog_api` が持つ関数は、`process` (`backend_process()`) と `panic` (`sdlog_panic()`) の 2 つです。

`LOG_OUTPUT_DEFINE()` で、文字列にする出力を 2 つ作ります (`boot_output`、`error_output`)。それぞれ、32 バイトの作業用のバッファを持ち、32 バイトずつ、`boot_out()`、`error_out()` に渡します。長い行は、何回かに分けて渡されます。

書式のフラグ (`SDLOG_FLAGS`) は、レベルと、起動からの時刻を付け、改行を LF だけにします。

## backend_process

ログ 1 件ごとに呼ばれます。

```text
k_uptime_get() < BOOT_WINDOW_MS (60 秒) → boot_output に文字列にする → boot_ring へ
ログのレベルが LOG_LEVEL_ERR           → error_output に文字列にする → error_ring へ
```

起動の 60 秒の間のエラーは、両方のファイルに入ります。

## リングバッファへの入れ方 (store)

`ring_buf_put()` で入れます。入りきらなかった分は、捨てて、捨てたバイト数を `boot_dropped`、`error_dropped` (アトミックな変数) に足します。バッファがいっぱいのときは、**新しい文字列を捨てる**ので、起動の最初のログが残ります。

バッファの大きさは小さいです (`CONFIG_THERMO_SDLOG_BOOT_BUF_SIZE` は 768 バイト、`CONFIG_THERMO_SDLOG_ERROR_BUF_SIZE` は 256 バイト)。ESP32-C3 の静的 RAM が、BLE、WiFi、TLS で、ほぼ使われているためです (`Kconfig` の説明に、残りは約 1 KB とあります)。起動で、SD カードのマウントまでに出るログが 768 バイトを超えると、超えた分は捨てられ、`boot.log` に欠けが出ます。

## sdlog_flush

```text
ready でなければ → 何もしない
flush_boot()      失敗しなければ
flush_error()
失敗 → ready = false、"Could not write the log to the SD card, stop saving" を警告して返る
report_dropped("boot")、report_dropped("error")   ← 捨てたログがあれば、エラーとして知らせる
```

- **書き込みに失敗すると、書き込みを止めます。** カードが外れたときに、失敗のログが、また失敗のログを生む連鎖を、避けるためです。再開は、次の `sdlog_start()` までです。
- `report_dropped()` は、`LOG_ERR("The boot log buffer was full, N bytes were not saved")` を出します。このログ自身も、バックエンドを通って、リングバッファに入り、次の `sdlog_flush()` で、`error.log` に書かれます。

### flush_boot

リングバッファが空なら、何もしません。最初の書き込みは `FS_O_TRUNC` (ファイルを作り直す)、2 回目からは `FS_O_APPEND` (追記) で開きます (`boot_created`)。起動のたびに、前回のログが消えて、今回のログになります。起動の 60 秒より後でも、リングバッファに残りがあれば、書きます。60 秒より後には、新しいログは入りません。

### flush_error

リングバッファが空なら、何もしません。空でなければ、`rotate_error()` で上限を確認して、`FS_O_APPEND` で書きます。

### rotate_error

`error.log` の大きさが `CONFIG_THERMO_SDLOG_ERROR_MAX_BYTES` 以上なら、`error.old` を消して (`fs_unlink`)、`error.log` を `error.old` に移します (`fs_rename`)。どちらも、失敗しても続けます。移せなかったときは、`error.log` に追記を続けて、次の書き込みで、もう一度移そうとします。つまり、保存するエラーのログは、最大で 2 ファイルぶん (約 128 KB) です。

### write_ring

```text
fs_open(path, CREATE | WRITE | フラグ)  失敗 → そのまま返る (リングバッファは残る。あとで書き直す)
リングバッファが空になるまで:
    128 バイトずつ取り出して、fs_write()
    書けたバイト数が違えば -EIO (取り出した分は、戻らない)
fs_close()
```

ファイルを開けなかったときは、ログを失いません。書き込みの途中で失敗したときは、取り出した分が失われます。

## 気づいた点

- 書き込みが止まったあと (`ready = false`)、`backlog.c` の SD カードの状態 (`backlog_ready`) が true のままだと、`thermo sd` を実行しても、`backlog_init()` が呼ばれず (`direct_sd_check()` は、使えているときは、マウントし直しません)、`sdlog_start()` も呼ばれません。ログの保存を再開するには、再起動、または、`backlog_init()` が呼ばれる操作が要ります。
- 起動のログは、`direct_init()` の中の `backlog_init()` でマウントが終わるまで、RAM にしか残りません。マウント前に 768 バイトを超えて出たログは失われます。`main.c` は、起動画面の約 3 秒の前に `sdlog_flush()` を呼んで、そのあいだに溜まりすぎないようにしています。
- `sdlog_flush()` は、メインスレッドだけが呼びます。ログのバックエンド (`process()`) と、`sdlog_flush()` が、同じリングバッファを別のスレッドから扱います。Zephyr の `ring_buf` は、生産者が 1 つ、消費者が 1 つのとき、ロックなしで使えます。`process()` が複数の文脈から同時に呼ばれると、その前提が崩れますが、Zephyr のログのコアが、バックエンドの呼び出しを直列にすることに頼っています。

## 関連

- [backlog.md](backlog.md): `backlog_init()` が `sdlog_start()` を呼ぶ
- [main.md](main.md): `sdlog_flush()` の呼び出し
- [wifi_shell.md](wifi_shell.md): シェルの `thermo sd`
- [../../../DEBUG_LOG.md](../../../DEBUG_LOG.md): デバッグのログの取り方
- [index.md](index.md): このディレクトリの文書の一覧
