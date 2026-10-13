# カーネル

カーネル (`zephyr/kernel.h`) の、使っている API。スレッド、スリープ、セマフォ、メッセージキュー、遅延ワーク。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

## スレッド

### k_thread_create

```c
k_tid_t k_thread_create(struct k_thread *new_thread, k_thread_stack_t *stack,
                       size_t stack_size, k_thread_entry_t entry,
                       void *p1, void *p2, void *p3,
                       int prio, uint32_t options, k_timeout_t delay);
```

スレッドを作って、実行できる状態にする。遅延なしなら、スケジューラが、今のスレッドを止めて、すぐ新しいスレッドを実行することがある。

- `new_thread`: 初期化前の `struct k_thread`。`stack` / `stack_size`: スタックと、そのバイト数。`K_THREAD_STACK_DEFINE()` で作ったスタックを使い、サイズには、`K_THREAD_STACK_SIZEOF(stack)` か、`K_THREAD_STACK_DEFINE()` に渡した値を使う (`sizeof(stack)` など、ほかの値は未定義動作)。
- `entry` / `p1` `p2` `p3`: スレッドの入口の関数と、その 3 つの引数。`prio`: 優先度。`options`: スレッドのオプション (アーキテクチャ依存。`K_ESSENTIAL` `K_FP_REGS` など。`|` で複数指定)。`delay`: 開始までの遅延。遅延なしは `K_NO_WAIT`。
- 戻り値: 新しいスレッドの ID。

使用箇所: `app/common/cloud.c:240`

### k_thread_name_set

```c
int k_thread_name_set(k_tid_t thread, const char *str);
```

スレッドの名前を付ける。`CONFIG_THREAD_MONITOR` が有効なときの、トレースとデバッグで使う。

- `thread`: 名前を付けるスレッド。`NULL` なら、今のスレッド。
- 戻り値: 成功は 0。`-EFAULT`: 文字列のメモリにアクセスできない。`-ENOSYS`: スレッド名の設定が無効。`-EINVAL`: 名前が長すぎる。

使用箇所: `app/common/cloud.c:242`

### k_thread_abort

```c
void k_thread_abort(k_tid_t thread);
```

スレッドを、永久に止める。スレッドは、レディキュー、タイムアウトキュー、カーネルオブジェクトの待ちキューから外れる。ただし、スレッドが持っているカーネルの資源 (ミューテックスやメモリブロックなど) は、解放されない。後始末は、呼び出し側の責任。

- `thread`: 止めるスレッドの ID。
- 戻り値が返った後は、そのスレッドが、動いていないことと、動けるようにならないことが保証される。

使用箇所: `app/common/cloud.c:254`

## スリープと時刻

### k_sleep

```c
int32_t k_sleep(k_timeout_t timeout);
```

今のスレッドを、指定の時間だけ眠らせる。

- `timeout`: 眠る時間 (`K_MSEC()` `K_SECONDS()` など)。
- 戻り値: 時間が経ったら 0。`k_wakeup()` で起こされたら、残りの時間 (ミリ秒に切り上げ。`INT_MAX` で切り詰める)。

使用箇所: `app/common/cloud.c:155 (ほか 3 行)`, `app/thermo-gateway/src/main.c:93`

### k_msleep

```c
int32_t k_msleep(int32_t ms);
```

今のスレッドを、ミリ秒で指定した時間だけ眠らせる。

- `ms`: 眠るミリ秒。
- 戻り値: 時間が経ったか、`k_wakeup()` で起こされたら 0。そうでなければ、残りの時間 (ミリ秒に切り上げ)。

使用箇所: `app/thermo-node/src/main.c:335 (ほか 1 行)`

### k_uptime_get

```c
int64_t k_uptime_get(void);
```

システムが起動してからの時間を、ミリ秒で返す。

- 単位はミリ秒だが、分解能がミリ秒とは限らない。実際の分解能は、`CONFIG_SYS_CLOCK_TICKS_PER_SEC` で決まる。

使用箇所: `app/common/ntp.c:59 (ほか 1 行)`, `app/thermo-gateway/src/ble.c:543 (ほか 1 行)`, `app/thermo-node/src/main.c:146 (ほか 2 行)`, `app/thermo-node/src/sdlog.c:165`

### k_uptime_get_32

```c
uint32_t k_uptime_get_32(void);
```

システムが起動してからの時間の、下位 32 ビットを、ミリ秒で返す。

- `k_uptime_get()` に比べて、利点はほとんどない (アプリが `2^32` ティックを超えて動かないと分かっているときだけ)。割り込みを止めて、64 ビットの計算をすることがある。

使用箇所: `app/common/cloud.c:274 (ほか 3 行)`, `app/thermo-gateway/src/main.c:154`

## セマフォ

### k_sem_take

```c
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
```

セマフォを取る。

- `sem`: セマフォ。`timeout`: 待つ時間 (`K_NO_WAIT` `K_FOREVER` か、時間)。割り込みから呼ぶときは `K_NO_WAIT`。
- 戻り値: 取れたら 0。`-EBUSY`: 待たずに戻った (取れなかった)。`-EAGAIN`: 待ち時間が切れた、または待っている間にセマフォがリセットされた。

使用箇所: `app/common/wifi_link.c:108`

### k_sem_give

```c
void k_sem_give(struct k_sem *sem);
```

セマフォを与える。最大のカウントに達していたら、何もしない。割り込みから呼べる。

使用箇所: `app/common/wifi_link.c:168 (ほか 1 行)`

### k_sem_reset

```c
void k_sem_reset(struct k_sem *sem);
```

セマフォのカウントを 0 にする。待っている `k_sem_take()` は、`-EAGAIN` で中止される。

使用箇所: `app/common/wifi_link.c:99`

## メッセージキュー

### k_msgq_put

```c
int k_msgq_put(struct k_msgq *msgq, const void *data, k_timeout_t timeout);
```

メッセージキューの末尾に、メッセージを送る。メッセージの内容は、`data` から、キューにコピーされる (ポインタは、保持されない)。割り込みから呼べる。

- `timeout`: 待つ時間 (`K_NO_WAIT` `K_FOREVER` か、時間)。割り込みから呼ぶときは `K_NO_WAIT`。
- 戻り値: 送れたら 0。`-ENOMSG`: 待たずに戻った (キューが満杯)、またはキューが空にされた。`-EAGAIN`: 待ち時間が切れた。

使用箇所: `app/common/cloud.c:714`

### k_msgq_get

```c
int k_msgq_get(struct k_msgq *msgq, void *data, k_timeout_t timeout);
```

メッセージキューから、メッセージを、先入れ先出しで受け取る。割り込みから呼べる。

- `data`: 受け取ったメッセージを入れる領域。`timeout`: 待つ時間。
- 戻り値: 受け取れたら 0。`-ENOMSG`: 待たずに戻った (キューが空)、またはキューが空にされた。`-EAGAIN`: 待ち時間が切れた。

使用箇所: `app/common/cloud.c:642`

### k_msgq_num_used_get

```c
uint32_t k_msgq_num_used_get(struct k_msgq *msgq);
```

メッセージキューに入っているメッセージの数を返す。

使用箇所: `app/common/cloud.c:388`

### k_msgq_purge

```c
void k_msgq_purge(struct k_msgq *msgq);
```

メッセージキューの、受け取っていないメッセージを、全て捨てる。キューに送ろうとして待っているスレッドは、`-ENOMSG` で起こされる。

使用箇所: `app/common/cloud.c:341`

## ワーク (遅延実行)

### k_work_schedule

```c
int k_work_schedule(struct k_work_delayable *dwork, k_timeout_t delay);
```

何もしていない遅延ワークを、指定の時間のあとに、システムのワークキューに登録する。すでに登録済みなら、時間は、そのままにする。

- `dwork`: 遅延ワーク。`delay`: 登録するまでの時間。`K_NO_WAIT` なら、すぐ登録する。
- 戻り値: `k_work_schedule_for_queue()` と同じ。

使用箇所: `app/thermo-gateway/src/ble.c:786`, `app/thermo-node/src/buzzer.c:78 (ほか 1 行)`

### k_work_reschedule

```c
int k_work_reschedule(struct k_work_delayable *dwork, k_timeout_t delay);
```

遅延ワークを、指定の時間のあとに、システムのワークキューに登録する。すでに登録済みなら、時間を、新しく数え直す。

- `dwork`: 遅延ワーク。`delay`: 登録するまでの時間。
- 戻り値: `k_work_reschedule_for_queue()` と同じ。

使用箇所: `app/thermo-gateway/src/ble.c:467 (ほか 2 行)`

### k_work_cancel_delayable

```c
int k_work_cancel_delayable(struct k_work_delayable *dwork);
```

遅延ワークを取り消す。登録や実行待ちなら、取り消す。取り消しの完了は待たない。割り込みから呼べる。

- 戻ったときも、ワークがまだ動いていることがある。動いていないことを確かめるには、`k_work_flush_delayable()` か `k_work_cancel_delayable_sync()` を使う。
- 取り消しても、再び登録することは防げない。取り消しが終わるまで、実行の登録だけを防ぐ。
- 戻り値: 取り消しの手順が済んだあとの、`k_work_delayable_busy_get()` の状態。

使用箇所: `app/thermo-node/src/buzzer.c:73`

## マクロ

### K_MSEC

```c
#define K_MSEC(ms)
```

ミリ秒を、`k_timeout_t` にする。

使用箇所: `app/common/cloud.c:155 (ほか 2 行)`, `app/thermo-gateway/src/ble.c:691`, `app/thermo-node/src/buzzer.c:113`

### K_SECONDS

```c
#define K_SECONDS(s)
```

秒を、`k_timeout_t` にする。

使用箇所: `app/common/cloud.c:161 (ほか 3 行)`, `app/thermo-gateway/src/ble.c:583`, `app/thermo-gateway/src/main.c:93`, `app/thermo-node/src/wifi_time.c:73 (ほか 1 行)`

### K_SEM_DEFINE

```c
#define K_SEM_DEFINE(name, initial_count, count_limit)
```

セマフォを、静的に定義して、初期化する。初期のカウントと、カウントの上限を指定する。

使用箇所: `app/common/wifi_link.c:31`

### K_MSGQ_DEFINE

```c
#define K_MSGQ_DEFINE(q_name, q_msg_size, q_max_msgs, q_align)
```

メッセージキューを、静的に定義して、初期化する。メッセージの大きさ、最大の数、アラインメントを指定する。

使用箇所: `app/common/cloud.c:89`

### K_THREAD_STACK_DEFINE

```c
#define K_THREAD_STACK_DEFINE(sym, size)
```

スレッドのスタックを、静的に定義する。`k_thread_create()` に渡す。

使用箇所: `app/common/cloud.c:124`

### K_THREAD_STACK_SIZEOF

```c
#define K_THREAD_STACK_SIZEOF(sym)
```

`K_THREAD_STACK_DEFINE()` で定義したスタックの大きさを返す。`k_thread_create()` の `stack_size` に使う。

使用箇所: `app/common/cloud.c:240`

### K_WORK_DELAYABLE_DEFINE

```c
#define K_WORK_DELAYABLE_DEFINE(work, work_handler)
```

遅延ワークを、静的に定義して、初期化する。`work_handler` は、ワークキューのスレッドで実行される。

使用箇所: `app/thermo-gateway/src/ble.c:112 (ほか 1 行)`, `app/thermo-node/src/buzzer.c:39`
