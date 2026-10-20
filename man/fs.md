# ファイルシステム

ファイルシステム (`zephyr/fs/fs.h`) の、使っている API。SD カードの FAT に、測定値の記録 (backlog) とログ (sdlog) を保存する。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

引数の「入出力」は、`in` が呼び出し側から渡す値、`out` が関数が書いて返す値、`in,out` が渡して書き換えられる値を表す。

## マウント

### fs_mount

```c
int fs_mount(struct fs_mount_t *mp);
```

ファイルシステムをマウントする。ファイルシステム固有のマウントの処理をして、マウント済みの一覧に、マウントポイントを加える。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `mp` | in,out | `fs_mount_t` 構造体。マウントに失敗したときは、変更されない。成功すると、ファイルシステムの仕組みが参照を保持するので、`fs_unmount()` が成功するまで、アプリは中身を変えない |

戻り値:

- 成功は 0。
- `-ENOENT`: その種類のファイルシステムが登録されていない。
- `-ENOTSUP`: ドライバが対応していない (`FS_MOUNT_FLAG_USE_DISK_ACCESS` を指定したが、ドライバが対応していない場合を含む)。
- `-EROFS`: フォーマットが必要だが `FS_MOUNT_FLAG_READ_ONLY` が指定されている。
- ほかの失敗は負の errno。

注意:

- ELM FAT のドライバが使えるマウントポイントは、`"/RAM:"` `"/NAND:"` `"/CF:"` `"/SD:"` `"/SD2:"` `"/USB:"` `"/USB2:"` `"/USB3:"` と、1 桁の数字 (`"/0:"` など)。SD カードは `"/SD:"`。

使用箇所: [`app/thermo-node/src/backlog.c:133`](../app/thermo-node/src/backlog.c#L133)

### fs_unmount

```c
int fs_unmount(struct fs_mount_t *mp);
```

ファイルシステムをアンマウントする。ファイルシステム固有のアンマウントの処理をして、マウント済みの一覧から、マウントポイントを外す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `mp` | in,out | アンマウントする `fs_mount_t` 構造体 |

戻り値:

- 成功は 0。
- `-EINVAL`: そのマウントポイントに、何もマウントされていない。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

使用箇所: [`app/thermo-node/src/backlog.c:125`](../app/thermo-node/src/backlog.c#L125)

## ファイル

### fs_file_t_init

```c
static inline void fs_file_t_init(struct fs_file_t *zfp);
```

`fs_file_t` を初期化する。`fs_open()` の前に、一度、呼ぶ必要がある。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `zfp` | out | 初期化するファイルオブジェクト |

使用箇所: [`app/thermo-node/src/backlog.c:512`](../app/thermo-node/src/backlog.c#L512), [`app/thermo-node/src/sdlog.c:246`](../app/thermo-node/src/sdlog.c#L246)

### fs_open

```c
int fs_open(struct fs_file_t *zfp, const char *file_name, fs_mode_t flags);
```

ファイルを開く。または、作る。開いたファイルは、使い終わったら `fs_close()` で閉じる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `zfp` | in,out | 初期化済みのファイルオブジェクト |
| `file_name` | in | ファイル名 |
| `flags` | in | モード。0、または次の組み合わせ。`FS_O_READ` (読み込み)、`FS_O_WRITE` (書き込み)、`FS_O_RDWR` (読み書き。`FS_O_READ \| FS_O_WRITE`)、`FS_O_CREATE` (なければ作る)、`FS_O_APPEND` (書く前に、必ず末尾へ動く)、`FS_O_TRUNC` (内容を空にする) |

戻り値:

- 成功は 0。
- `-EBUSY`: `zfp` が使用中。
- `-EINVAL`: ファイル名が不正。
- `-EROFS`: 読み込み専用のファイルを書き込みで開いた、または、`FS_MOUNT_FLAG_READ_ONLY` でマウントしたところに作ろうとした。
- `-ENOENT`: ファイルがない。
- `-ENOTSUP`: ドライバが対応していない。
- `-EACCES`: 書き込みで開かずに、`FS_O_TRUNC` を指定した。
- ほかの失敗は負の errno。

注意:

- `flags` を 0 にすると、あれば開くが、読み書きはできない。

使用箇所: [`app/thermo-node/src/backlog.c:513`](../app/thermo-node/src/backlog.c#L513), [`app/thermo-node/src/sdlog.c:247`](../app/thermo-node/src/sdlog.c#L247)

### fs_close

```c
int fs_close(struct fs_file_t *zfp);
```

ファイルを閉じる。ストリームを書き出して (flush) から、閉じる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `zfp` | in,out | 閉じるファイルオブジェクト |

戻り値:

- 成功は 0。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

使用箇所: [`app/thermo-node/src/backlog.c:191`](../app/thermo-node/src/backlog.c#L191) (ほか 5 行), [`app/thermo-node/src/sdlog.c:259`](../app/thermo-node/src/sdlog.c#L259)

### fs_read

```c
ssize_t fs_read(struct fs_file_t *zfp, void *ptr, size_t size);
```

ファイルから、最大 `size` バイトを読む。読めるデータが少なければ、`size` より少ない数を返すことがある。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `zfp` | in,out | ファイルオブジェクト |
| `ptr` | out | 読んだデータを入れるバッファ |
| `size` | in | 読むバイト数 |

戻り値:

- 成功は読んだバイト数 (0 以上)。
- `-EBADF`: 開いていない (閉じた) ファイル。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

使用箇所: [`app/thermo-node/src/backlog.c:425`](../app/thermo-node/src/backlog.c#L425) (ほか 1 行)

### fs_write

```c
ssize_t fs_write(struct fs_file_t *zfp, const void *ptr, size_t size);
```

ファイルに、`size` バイトを書こうとする。負の値を返したときは、ファイルの位置は進んでいない。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `zfp` | in,out | ファイルオブジェクト |
| `ptr` | in | 書くデータ |
| `size` | in | 書くバイト数 |

戻り値:

- 成功は書いたバイト数 (0 以上)。
- `-EBADF`: 開いていない (閉じた) ファイル。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

注意:

- 書いたバイト数が `size` より少ないときは、空き容量がないことがあるので、グローバルの `errno` を調べる。

使用箇所: [`app/thermo-node/src/backlog.c:592`](../app/thermo-node/src/backlog.c#L592) (ほか 1 行), [`app/thermo-node/src/sdlog.c:254`](../app/thermo-node/src/sdlog.c#L254)

### fs_seek

```c
int fs_seek(struct fs_file_t *zfp, off_t offset, int whence);
```

ファイルの位置を動かす。`offset` を、`whence` で決まる位置に足す。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `zfp` | in,out | ファイルオブジェクト |
| `offset` | in | 動かす量 |
| `whence` | in | 基準の位置。`FS_SEEK_SET` (先頭から)、`FS_SEEK_CUR` (今の位置から)、`FS_SEEK_END` (末尾から) |

戻り値:

- 成功は 0。
- `-EBADF`: 開いていないファイル。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

使用箇所: [`app/thermo-node/src/backlog.c:421`](../app/thermo-node/src/backlog.c#L421) (ほか 2 行)

## パス

### fs_stat

```c
int fs_stat(const char *path, struct fs_dirent *entry);
```

ファイルまたはディレクトリの状態を調べる。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `path` | in | パス |
| `entry` | out | 存在するときに、情報 (名前、種類、大きさ) を入れる `fs_dirent` |

戻り値:

- 成功は 0。
- `-EINVAL`: パスが不正。
- `-ENOENT`: ない。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

注意:

- ストレージ上のファイルは、閉じるまで、更新されていないことがある。

使用箇所: [`app/thermo-node/src/backlog.c:140`](../app/thermo-node/src/backlog.c#L140), [`app/thermo-node/src/sdlog.c:309`](../app/thermo-node/src/sdlog.c#L309)

### fs_rename

```c
int fs_rename(const char *from, const char *to);
```

ファイルまたはディレクトリの名前を変える (または移す)。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `from` | in | 移す元のパス |
| `to` | in | 移す先のパス。ファイルなら、ディレクトリではなく、ファイル名までのパス |

戻り値:

- 成功は 0。
- `-EINVAL`: 名前が不正、またはマウントポイントをまたぐ。
- `-EROFS`: 読み込み専用のファイル、または読み込み専用でマウントしている。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

注意:

- 移動先の途中のディレクトリは、あらかじめ存在する必要がある。
- 移動先にすでにあるものは、先に削除 (unlink) される。
- マウントポイントをまたぐ移動はできない。

使用箇所: [`app/thermo-node/src/sdlog.c:313`](../app/thermo-node/src/sdlog.c#L313)

### fs_unlink

```c
int fs_unlink(const char *path);
```

ファイルまたはディレクトリを削除する。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `path` | in | 削除するパス |

戻り値:

- 成功は 0。
- `-EINVAL`: 名前が不正。
- `-EROFS`: 読み込み専用のファイル、または読み込み専用でマウントしている。
- `-ENOTSUP`: ドライバが対応していない。
- ほかの失敗は負の errno。

使用箇所: [`app/thermo-node/src/sdlog.c:312`](../app/thermo-node/src/sdlog.c#L312)

## 定数と型

### FS_FATFS

```c
FS_FATFS  /* enum fs_type */
```

`fs_mount_t` の `type` に指定する、FAT ファイルシステムの種類。

使用箇所: [`app/thermo-node/src/backlog.c:81`](../app/thermo-node/src/backlog.c#L81)

### fs_mount_t

```c
struct fs_mount_t
```

マウントの情報。`fs_mount()` に渡す。マウント中は、中身を変えない。

| 引数 | 入出力 | 説明 |
|:---|:---:|:---|
| `type` | in | ファイルシステムの種類 (`FS_FATFS`) |
| `mnt_point` | in | マウントポイント |
| `fs_data` | in | ファイルシステムのデータ |
| `storage_dev` | in | 記憶装置の名前 |
| `flags` | in | マウントのフラグ |

注意:

- 表の引数は、構造体のメンバー。

使用箇所: [`app/thermo-node/src/backlog.c:80`](../app/thermo-node/src/backlog.c#L80)
