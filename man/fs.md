# ファイルシステム

ファイルシステム (`zephyr/fs/fs.h`) の、使っている API。SD カードの FAT に、測定値の記録 (backlog) とログ (sdlog) を保存する。

出典: Zephyr v4.3.0 のヘッダ (`include/zephyr/...`) の Doxygen コメントを、このプロジェクトでの使い方に絞って翻訳した。英語の原文が正。

## マウント

### fs_mount

```c
int fs_mount(struct fs_mount_t *mp);
```

ファイルシステムをマウントする。ファイルシステム固有のマウントの処理をして、マウント済みの一覧に、マウントポイントを加える。

- `mp`: `fs_mount_t` 構造体。マウントに失敗したときは、変更されない。成功すると、ファイルシステムの仕組みが参照を保持するので、`fs_unmount()` が成功するまで、アプリは、構造体の中身を変えない。
- ELM FAT のドライバが使えるマウントポイントは、`"/RAM:"` `"/NAND:"` `"/CF:"` `"/SD:"` `"/SD2:"` `"/USB:"` `"/USB2:"` `"/USB3:"` と、1 桁の数字 (`"/0:"` など)。SD カードは `"/SD:"`。
- 戻り値: 成功は 0。`-ENOENT`: その種類のファイルシステムが登録されていない。`-ENOTSUP`: ドライバが対応していない (`FS_MOUNT_FLAG_USE_DISK_ACCESS` を指定したが、ドライバが対応していない場合を含む)。`-EROFS`: フォーマットが必要だが `FS_MOUNT_FLAG_READ_ONLY` が指定されている。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:121`

### fs_unmount

```c
int fs_unmount(struct fs_mount_t *mp);
```

ファイルシステムをアンマウントする。ファイルシステム固有のアンマウントの処理をして、マウント済みの一覧から、マウントポイントを外す。

- 戻り値: 成功は 0。`-EINVAL`: そのマウントポイントに、何もマウントされていない。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:113`

## ファイル

### fs_file_t_init

```c
static inline void fs_file_t_init(struct fs_file_t *zfp);
```

`fs_file_t` を初期化する。`fs_open()` の前に、一度、呼ぶ必要がある。

使用箇所: `app/thermo-node/src/backlog.c:481`, `app/thermo-node/src/sdlog.c:246`

### fs_open

```c
int fs_open(struct fs_file_t *zfp, const char *file_name, fs_mode_t flags);
```

ファイルを開く。または、作る。開いたファイルは、使い終わったら `fs_close()` で閉じる。

- `zfp`: 初期化済みのファイルオブジェクト。`file_name`: ファイル名。`flags`: モード。0、または次の組み合わせ。`FS_O_READ` (読み込み)、`FS_O_WRITE` (書き込み)、`FS_O_RDWR` (読み書き。`FS_O_READ | FS_O_WRITE`)、`FS_O_CREATE` (なければ作る)、`FS_O_APPEND` (書く前に、必ず末尾へ動く)、`FS_O_TRUNC` (内容を空にする)。
- `flags` を 0 にすると、あれば開くが、読み書きはできない。
- 戻り値: 成功は 0。`-EBUSY`: `zfp` が使用中。`-EINVAL`: ファイル名が不正。`-EROFS`: 読み込み専用のファイルを書き込みで開いた、または、`FS_MOUNT_FLAG_READ_ONLY` でマウントしたところに作ろうとした。`-ENOENT`: ファイルがない。`-ENOTSUP`: ドライバが対応していない。`-EACCES`: 書き込みで開かずに、`FS_O_TRUNC` を指定した。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:482`, `app/thermo-node/src/sdlog.c:247`

### fs_close

```c
int fs_close(struct fs_file_t *zfp);
```

ファイルを閉じる。ストリームを書き出して(flush)から、閉じる。

- 戻り値: 成功は 0。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:180 (ほか 5 行)`, `app/thermo-node/src/sdlog.c:259`

### fs_read

```c
ssize_t fs_read(struct fs_file_t *zfp, void *ptr, size_t size);
```

ファイルから、最大 `size` バイトを読む。読めるデータが少なければ、`size` より少ない数を返すことがある。

- `ptr`: 読んだデータを入れるバッファ。`size`: 読むバイト数。
- 戻り値: 成功は読んだバイト数 (0 以上)。`-EBADF`: 開いていない (閉じた) ファイル。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:418 (ほか 1 行)`

### fs_write

```c
ssize_t fs_write(struct fs_file_t *zfp, const void *ptr, size_t size);
```

ファイルに、`size` バイトを書こうとする。負の値を返したときは、ファイルの位置は進んでいない。

- `ptr`: 書くデータ。`size`: 書くバイト数。
- 戻り値: 成功は書いたバイト数 (0 以上)。`size` より少ないときは、空き容量がないことがあるので、グローバルの `errno` を調べる。`-EBADF`: 開いていない (閉じた) ファイル。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:562 (ほか 1 行)`, `app/thermo-node/src/sdlog.c:254`

### fs_seek

```c
int fs_seek(struct fs_file_t *zfp, off_t offset, int whence);
```

ファイルの位置を動かす。`offset` を、`whence` で決まる位置に足す。

- `whence`: `FS_SEEK_SET` (先頭から)、`FS_SEEK_CUR` (今の位置から)、`FS_SEEK_END` (末尾から)。
- 戻り値: 成功は 0。`-EBADF`: 開いていないファイル。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:414 (ほか 2 行)`

## パス

### fs_stat

```c
int fs_stat(const char *path, struct fs_dirent *entry);
```

ファイルまたはディレクトリの状態を調べる。

- `path`: パス。`entry`: 存在するときに、情報を入れる `fs_dirent` (名前、種類、大きさ)。
- ストレージ上のファイルは、閉じるまで、更新されていないことがある。
- 戻り値: 成功は 0。`-EINVAL`: パスが不正。`-ENOENT`: ない。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/backlog.c:128`, `app/thermo-node/src/sdlog.c:309`

### fs_rename

```c
int fs_rename(const char *from, const char *to);
```

ファイルまたはディレクトリの名前を変える (または移す)。

- 移動先の途中のディレクトリは、あらかじめ存在する必要がある。ファイルなら、移動先は、ディレクトリではなく、ファイル名までのパス。移動先にすでにあるものは、先に削除 (unlink) される。
- マウントポイントをまたぐ移動はできない。
- 戻り値: 成功は 0。`-EINVAL`: 名前が不正、またはマウントポイントをまたぐ。`-EROFS`: 読み込み専用のファイル、または読み込み専用でマウントしている。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/sdlog.c:313`

### fs_unlink

```c
int fs_unlink(const char *path);
```

ファイルまたはディレクトリを削除する。

- 戻り値: 成功は 0。`-EINVAL`: 名前が不正。`-EROFS`: 読み込み専用のファイル、または読み込み専用でマウントしている。`-ENOTSUP`: ドライバが対応していない。ほかの失敗は負の errno。

使用箇所: `app/thermo-node/src/sdlog.c:312`

## 定数と型

### FS_FATFS

```c
FS_FATFS  /* enum fs_type */
```

`fs_mount_t` の `type` に指定する、FAT ファイルシステムの種類。

使用箇所: `app/thermo-node/src/backlog.c:70`

### fs_mount_t

```c
struct fs_mount_t
```

マウントの情報 (種類 `type`、マウントポイント `mnt_point`、ファイルシステムのデータ `fs_data`、記憶装置の名前 `storage_dev`、フラグ `flags`)。`fs_mount()` に渡す。マウント中は、中身を変えない。

使用箇所: `app/thermo-node/src/backlog.c:69`
