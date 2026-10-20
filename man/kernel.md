# カーネル

カーネル (`zephyr/kernel.h`) の、使っている API。スレッド、スリープ、セマフォ、メッセージキュー、遅延ワーク。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## スレッド

### k_thread_create

```c
k_tid_t k_thread_create(struct k_thread *new_thread, k_thread_stack_t *stack,
                       size_t stack_size, k_thread_entry_t entry,
                       void *p1, void *p2, void *p3,
                       int prio, uint32_t options, k_timeout_t delay);
```

スレッドを作って、実行できる状態にする。遅延なしなら、スケジューラが、今のスレッドを止めて、すぐ新しいスレッドを実行することがある。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `new_thread` | out | 初期化前の `struct k_thread`。ここにスレッドの情報が作られる |
| `stack` | in | スタック。`K_THREAD_STACK_DEFINE()` で作ったもの |
| `stack_size` | in | スタックのバイト数。`K_THREAD_STACK_SIZEOF(stack)` か、`K_THREAD_STACK_DEFINE()` に渡した値 (`sizeof(stack)` など、ほかの値は未定義動作) |
| `entry` | in | スレッドの入口の関数 |
| `p1` | in | 入口の関数の 1 つ目の引数 |
| `p2` | in | 入口の関数の 2 つ目の引数 |
| `p3` | in | 入口の関数の 3 つ目の引数 |
| `prio` | in | 優先度 |
| `options` | in | スレッドのオプション (アーキテクチャ依存。`K_ESSENTIAL` `K_FP_REGS` など。`\|` で複数指定) |
| `delay` | in | 開始までの遅延。遅延なしは `K_NO_WAIT` |

戻り値: 新しいスレッドの ID。

使用箇所: [`app/common/cloud.c:240`](../app/common/cloud.c#L240)

### k_thread_name_set

```c
int k_thread_name_set(k_tid_t thread, const char *str);
```

スレッドの名前を付ける。`CONFIG_THREAD_MONITOR` が有効なときの、トレースとデバッグで使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `thread` | in | 名前を付けるスレッド。`NULL` なら、今のスレッド |
| `str` | in | 名前の文字列 |

戻り値:

- 成功は 0。
- `-EFAULT`: 文字列のメモリにアクセスできない。
- `-ENOSYS`: スレッド名の設定が無効。
- `-EINVAL`: 名前が長すぎる。

使用箇所: [`app/common/cloud.c:242`](../app/common/cloud.c#L242)

### k_thread_abort

```c
void k_thread_abort(k_tid_t thread);
```

スレッドを、永久に止める。スレッドは、レディキュー、タイムアウトキュー、カーネルオブジェクトの待ちキューから外れる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `thread` | in | 止めるスレッドの ID |

注意:

- スレッドが持っているカーネルの資源 (ミューテックスやメモリブロックなど) は、解放されない。後始末は、呼び出し側の責任。
- 関数が戻った後は、そのスレッドが、動いていないことと、動けるようにならないことが保証される。

使用箇所: [`app/common/cloud.c:254`](../app/common/cloud.c#L254)

## スリープと時刻

### k_sleep

```c
int32_t k_sleep(k_timeout_t timeout);
```

今のスレッドを、指定の時間だけ眠らせる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `timeout` | in | 眠る時間 (`K_MSEC()` `K_SECONDS()` など) |

戻り値: 時間が経ったら 0。`k_wakeup()` で起こされたら、残りの時間 (ミリ秒に切り上げ。`INT_MAX` で切り詰める)。

使用箇所: [`app/common/cloud.c:155`](../app/common/cloud.c#L155) (ほか 3 行), [`app/thermo-gateway/src/main.c:93`](../app/thermo-gateway/src/main.c#L93)

### k_msleep

```c
int32_t k_msleep(int32_t ms);
```

今のスレッドを、ミリ秒で指定した時間だけ眠らせる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ms` | in | 眠るミリ秒 |

戻り値: 時間が経ったか、`k_wakeup()` で起こされたら 0。そうでなければ、残りの時間 (ミリ秒に切り上げ)。

使用箇所: [`app/thermo-node/src/main.c:335`](../app/thermo-node/src/main.c#L335) (ほか 1 行)

### k_uptime_get

```c
int64_t k_uptime_get(void);
```

システムが起動してからの時間を、ミリ秒で返す。

戻り値: 起動からの時間 [ms]。

注意:

- 単位はミリ秒だが、分解能がミリ秒とは限らない。実際の分解能は、`CONFIG_SYS_CLOCK_TICKS_PER_SEC` で決まる。

使用箇所: [`app/common/ntp.c:59`](../app/common/ntp.c#L59) (ほか 1 行), [`app/thermo-gateway/src/ble.c:543`](../app/thermo-gateway/src/ble.c#L543) (ほか 1 行), [`app/thermo-node/src/main.c:146`](../app/thermo-node/src/main.c#L146) (ほか 2 行), [`app/thermo-node/src/sdlog.c:165`](../app/thermo-node/src/sdlog.c#L165)

### k_uptime_get_32

```c
uint32_t k_uptime_get_32(void);
```

システムが起動してからの時間の、下位 32 ビットを、ミリ秒で返す。

戻り値: 起動からの時間の下位 32 ビット [ms]。

注意:

- `k_uptime_get()` に比べて、利点はほとんどない (アプリが `2^32` ティックを超えて動かないと分かっているときだけ)。割り込みを止めて、64 ビットの計算をすることがある。

使用箇所: [`app/common/cloud.c:274`](../app/common/cloud.c#L274) (ほか 3 行), [`app/thermo-gateway/src/main.c:154`](../app/thermo-gateway/src/main.c#L154)

### k_ticks_to_ms_ceil32

```c
#define k_ticks_to_ms_ceil32(t)
```

ティックの時間を、ミリ秒に換算する。32 ビットで計算し、切り上げる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `t` | in | ティックの時間 (`uint64_t`) |

戻り値: ミリ秒の時間 (`uint32_t`)。

使用箇所: [`app/common/ntp.c:74`](../app/common/ntp.c#L74), [`app/thermo-node/src/wifi_time.c:115`](../app/thermo-node/src/wifi_time.c#L115)

## セマフォ

### k_sem_take

```c
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
```

セマフォを取る。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `sem` | in,out | セマフォ |
| `timeout` | in | 待つ時間 (`K_NO_WAIT` `K_FOREVER` か、時間)。割り込みから呼ぶときは `K_NO_WAIT` |

戻り値:

- 取れたら 0。
- `-EBUSY`: 待たずに戻った (取れなかった)。
- `-EAGAIN`: 待ち時間が切れた、または待っている間にセマフォがリセットされた。

使用箇所: [`app/common/wifi_link.c:108`](../app/common/wifi_link.c#L108)

### k_sem_give

```c
void k_sem_give(struct k_sem *sem);
```

セマフォを与える。最大のカウントに達していたら、何もしない。割り込みから呼べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `sem` | in,out | セマフォ |

使用箇所: [`app/common/wifi_link.c:168`](../app/common/wifi_link.c#L168) (ほか 1 行)

### k_sem_reset

```c
void k_sem_reset(struct k_sem *sem);
```

セマフォのカウントを 0 にする。待っている `k_sem_take()` は、`-EAGAIN` で中止される。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `sem` | in,out | セマフォ |

使用箇所: [`app/common/wifi_link.c:99`](../app/common/wifi_link.c#L99)

## メッセージキュー

メッセージキュー (`struct k_msgq`) は、決まった大きさのメッセージを、決まった数だけ溜めておけるキュー。スレッドの間や、割り込みとスレッドの間で、データを受け渡すのに使う。

- **先入れ先出し**: 先に送ったメッセージから、順に受け取る。
- **コピーで受け渡す**: 送るときは、メッセージの内容を、キューの中のバッファ (リングバッファ) にコピーする。受け取るときは、バッファから、呼び出し側の領域にコピーする。ポインタを渡すのではないので、送ったあと、元の変数を書き換えてもよい。
- **大きさと数は、作るときに決める**: メッセージの大きさ (`q_msg_size`) と、最大の数 (`q_max_msgs`) は、あとから変えられない。バッファは、`q_msg_size × q_max_msgs` バイト。
- **満杯と空のときは、待つか、すぐ戻るか、選ぶ**: `k_msgq_put()` は満杯のとき、`k_msgq_get()` は空のときに、`timeout` の間だけ待つ。`K_NO_WAIT` なら、待たずに `-ENOMSG` で戻る。割り込みから呼ぶときは、`K_NO_WAIT` にする。
- **排他は、キューがする**: 複数のスレッドから、同時に送ったり、受け取ったりしてよい (キューの中で、ロックする)。

### 定義と宣言

キューは、`K_MSGQ_DEFINE` で、静的に定義する (下の「マクロ」)。このマクロは、変数の定義そのものに展開される。

```c
/* K_MSGQ_DEFINE(cloud_sample_q, sizeof(struct sample), QUEUE_LEN, 4); の展開 (概略) */
static char __noinit __aligned(4) _k_fifo_buf_cloud_sample_q[QUEUE_LEN * sizeof(struct sample)];
STRUCT_SECTION_ITERABLE(k_msgq, cloud_sample_q) = /* struct k_msgq cloud_sample_q (専用のセクションに置く) */
    Z_MSGQ_INITIALIZER(cloud_sample_q, _k_fifo_buf_cloud_sample_q, sizeof(struct sample), QUEUE_LEN);
```

- バッファ (`_k_fifo_buf_<名前>`) は `static`、キューの本体 (`struct k_msgq <名前>`) は、`static` ではない (全てのファイルから見える名前になる)。
- 同じファイルの中では、定義より後ろなら、宣言なしで使える。
- ほかのファイルから使うときは、`extern struct k_msgq <名前>;` と宣言する (このプロジェクトでは、`extern` の宣言は、`.c` ではなく、ヘッダに書く。`CODING_STYLE.md` の「MISRA-C 準拠ルール」の 7)。
- マクロの前に、`static` を付けることはできない (展開の先頭が `static char ...` なので、`static static` になる)。
- `static` で定義するマクロ `K_MSGQ_DEFINE_STATIC` は、Zephyr v4.4 で加わったもので、このプロジェクトの v4.3.0 には、ない。
- 実行時に初期化する `k_msgq_init()` もある (このプロジェクトでは、使っていない)。

### このプロジェクトでの使い方

AWS IoT Core へ送る値のキュー `cloud_sample_q` ([`app/common/cloud.c:89`](../app/common/cloud.c#L89)) だけで使う。

| 項目 | 内容 |
|:---|:---|
| メッセージ | `struct sample` (Thermo ノードか SwitchBot の値と、受信したときの稼働時間、測った UNIX 時刻) |
| 最大の数 | `QUEUE_LEN` (16) |
| 送る側 | `cloud_publish_temperature()` などから、`enqueue_sample()` で送る。ゲートウェイは、Bluetooth のスレッドから、ノードは、ゲートウェイにつながらないとき (`direct.c`) に呼ぶ。`K_NO_WAIT` で送り、満杯なら、値を捨てて `-ENOMSG` を返す |
| 受け取る側 | 送信のスレッド (`run_session()`)。最大 `SESSION_TICK_MS` (1 秒) 待って受け取り、MQTT で publish する。待ち時間が切れたら、受信と keep alive の確認に進む |
| 空にする | 送信を再び始めるとき (`cloud_set_enabled(true)`)、止めていた間の古い値を捨てる |
| 数を調べる | `cloud_pending()`。SD カードの未送信の値を、キューを満杯にしないように入れるのに使う |

### k_msgq_put

```c
int k_msgq_put(struct k_msgq *msgq, const void *data, k_timeout_t timeout);
```

メッセージキューの末尾に、メッセージを送る。メッセージの内容は、`data` から、キューにコピーされる (ポインタは、保持されない)。割り込みから呼べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `msgq` | in,out | メッセージキュー |
| `data` | in | 送るメッセージ |
| `timeout` | in | 待つ時間 (`K_NO_WAIT` `K_FOREVER` か、時間)。割り込みから呼ぶときは `K_NO_WAIT` |

戻り値:

- 送れたら 0。
- `-ENOMSG`: 待たずに戻った (キューが満杯)、またはキューが空にされた。
- `-EAGAIN`: 待ち時間が切れた。

使用箇所: [`app/common/cloud.c:714`](../app/common/cloud.c#L714)

### k_msgq_get

```c
int k_msgq_get(struct k_msgq *msgq, void *data, k_timeout_t timeout);
```

メッセージキューから、メッセージを、先入れ先出しで受け取る。割り込みから呼べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `msgq` | in,out | メッセージキュー |
| `data` | out | 受け取ったメッセージを入れる領域 |
| `timeout` | in | 待つ時間 (`K_NO_WAIT` `K_FOREVER` か、時間)。割り込みから呼ぶときは `K_NO_WAIT` |

戻り値:

- 受け取れたら 0。
- `-ENOMSG`: 待たずに戻った (キューが空)、またはキューが空にされた。
- `-EAGAIN`: 待ち時間が切れた。

使用箇所: [`app/common/cloud.c:642`](../app/common/cloud.c#L642)

### k_msgq_num_used_get

```c
uint32_t k_msgq_num_used_get(struct k_msgq *msgq);
```

メッセージキューに入っているメッセージの数を返す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `msgq` | in | メッセージキュー |

戻り値: キューに入っているメッセージの数。

使用箇所: [`app/common/cloud.c:388`](../app/common/cloud.c#L388)

### k_msgq_purge

```c
void k_msgq_purge(struct k_msgq *msgq);
```

メッセージキューの、受け取っていないメッセージを、全て捨てる。キューに送ろうとして待っているスレッドは、`-ENOMSG` で起こされる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `msgq` | in,out | メッセージキュー |

使用箇所: [`app/common/cloud.c:341`](../app/common/cloud.c#L341)

## ワーク (遅延実行)

### k_work_schedule

```c
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
```

何もしていない遅延ワークを、指定の時間のあとに、システムのワークキューに登録する。すでに登録済みなら、時間は、そのままにする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dwork` | in,out | 遅延ワーク |
| `delay` | in | 登録するまでの時間。`K_NO_WAIT` なら、すぐ登録する |

戻り値: `k_work_schedule_for_queue()` と同じ。

使用箇所: [`app/thermo-gateway/src/ble.c:786`](../app/thermo-gateway/src/ble.c#L786), [`app/thermo-node/src/buzzer.c:78`](../app/thermo-node/src/buzzer.c#L78) (ほか 1 行)

### k_work_reschedule

```c
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
```

遅延ワークを、指定の時間のあとに、システムのワークキューに登録する。すでに登録済みなら、時間を、新しく数え直す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dwork` | in,out | 遅延ワーク |
| `delay` | in | 登録するまでの時間 |

戻り値: `k_work_reschedule_for_queue()` と同じ。

使用箇所: [`app/thermo-gateway/src/ble.c:467`](../app/thermo-gateway/src/ble.c#L467) (ほか 2 行)

### k_work_cancel_delayable

```c
int k_work_cancel_delayable(struct k_work_delayable *dwork);
```

遅延ワークを取り消す。登録や実行待ちなら、取り消す。取り消しの完了は待たない。割り込みから呼べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `dwork` | in,out | 遅延ワーク |

戻り値: 取り消しの手順が済んだあとの、`k_work_delayable_busy_get()` の状態。

注意:

- 戻ったときも、ワークがまだ動いていることがある。動いていないことを確かめるには、`k_work_flush_delayable()` か `k_work_cancel_delayable_sync()` を使う。
- 取り消しても、再び登録することは防げない。取り消しが終わるまで、実行の登録だけを防ぐ。

使用箇所: [`app/thermo-node/src/buzzer.c:73`](../app/thermo-node/src/buzzer.c#L73)

## マクロ

### K_MSEC

```c
#define K_MSEC(ms)
```

ミリ秒を、`k_timeout_t` にする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `ms` | in | ミリ秒 |

使用箇所: [`app/common/cloud.c:155`](../app/common/cloud.c#L155) (ほか 2 行), [`app/thermo-gateway/src/ble.c:691`](../app/thermo-gateway/src/ble.c#L691), [`app/thermo-node/src/buzzer.c:113`](../app/thermo-node/src/buzzer.c#L113)

### K_SECONDS

```c
#define K_SECONDS(s)
```

秒を、`k_timeout_t` にする。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `s` | in | 秒 |

使用箇所: [`app/common/cloud.c:161`](../app/common/cloud.c#L161) (ほか 3 行), [`app/thermo-gateway/src/ble.c:583`](../app/thermo-gateway/src/ble.c#L583), [`app/thermo-gateway/src/main.c:93`](../app/thermo-gateway/src/main.c#L93), [`app/thermo-node/src/wifi_time.c:73`](../app/thermo-node/src/wifi_time.c#L73) (ほか 1 行)

### K_SEM_DEFINE

```c
#define K_SEM_DEFINE(name, initial_count, count_limit)
```

セマフォを、静的に定義して、初期化する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `name` | in | セマフォの名前 |
| `initial_count` | in | 初期のカウント |
| `count_limit` | in | カウントの上限 |

使用箇所: [`app/common/wifi_link.c:31`](../app/common/wifi_link.c#L31)

### K_MSGQ_DEFINE

```c
#define K_MSGQ_DEFINE(q_name, q_msg_size, q_max_msgs, q_align)
```

メッセージキューを、静的に定義して、初期化する。キューのバッファには、`q_msg_size` バイトのメッセージが、`q_max_msgs` 個入る。バッファのアラインメントは、必須ではなく、`q_align` は 1 でよい。

定義したキューは、定義したファイルの外から、次の宣言で使える。

```c
extern struct k_msgq <name>;
```

このマクロには、`static` を付けられない (上の「定義と宣言」)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `q_name` | in | キューの名前 |
| `q_msg_size` | in | メッセージの大きさ [バイト] |
| `q_max_msgs` | in | 入れられるメッセージの最大の数 |
| `q_align` | in | バッファのアラインメント (2 のべき乗) |

使用箇所: [`app/common/cloud.c:89`](../app/common/cloud.c#L89)

### K_THREAD_STACK_DEFINE

```c
#define K_THREAD_STACK_DEFINE(sym, size)
```

スレッドのスタックを、静的に定義する。`k_thread_create()` に渡す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `sym` | in | スタックの名前 |
| `size` | in | スタックの大きさ [バイト] |

使用箇所: [`app/common/cloud.c:124`](../app/common/cloud.c#L124)

### K_THREAD_STACK_SIZEOF

```c
#define K_THREAD_STACK_SIZEOF(sym)
```

`K_THREAD_STACK_DEFINE()` で定義したスタックの大きさを返す。`k_thread_create()` の `stack_size` に使う。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `sym` | in | スタックの名前 |

戻り値: スタックの大きさ [バイト]。

使用箇所: [`app/common/cloud.c:240`](../app/common/cloud.c#L240)

### K_WORK_DELAYABLE_DEFINE

```c
#define K_WORK_DELAYABLE_DEFINE(work, work_handler)
```

遅延ワークを、静的に定義して、初期化する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `work` | in | 遅延ワークの名前 |
| `work_handler` | in | ワークキューのスレッドで実行されるハンドラ |

使用箇所: [`app/thermo-gateway/src/ble.c:112`](../app/thermo-gateway/src/ble.c#L112) (ほか 1 行), [`app/thermo-node/src/buzzer.c:39`](../app/thermo-node/src/buzzer.c#L39)
