# Zephyr API マニュアル (日本語訳)

このプロジェクトが使っている Zephyr (v4.3.0) の API を、日本語に訳したもの。Zephyr の全ての API ではなく、`app/` のソース (`thermo-node` `thermo-gateway` `common`。単体テストを除く) が呼んでいるものだけを、サブシステムごとに載せる。ただし、単体テストのモックに使っている FFF だけは、例外として載せる ([fff.md](fff.md))。

- 出典は、Zephyr v4.3.0 のヘッダの Doxygen コメント。意味を変えないように訳した。**英語の原文が正**なので、細かい挙動で迷ったら、ヘッダ (`zephyr/include/zephyr/...`) か、公式の文書を見る。
- 各 API に「使用箇所」を付けた。このリポジトリの、呼んでいる場所 (`ファイル:最初の行`。そこへのリンク) で、同じファイルで何度も呼ぶ場合は、「ほか N 行」と書く。
- 引数は、1 つ 1 行の表にして、「入出力」の列に `in` (渡す値)、`out` (関数が書いて返す値)、`in,out` (渡して書き換えられる値) を書いた。戻り値と注意は、表の後ろに書く。
- ソースを解析した文書は、`app/*/docs/` にある。このディレクトリは、そこから参照される API の、リファレンスにあたる。

| ファイル | 内容 | API の数 |
|:---|:---|---:|
| [bluetooth.md](bluetooth.md) | Bluetooth LE | 34 |
| [kernel.md](kernel.md) | カーネル | 25 |
| [drivers.md](drivers.md) | デバイスとドライバ | 32 |
| [display.md](display.md) | 表示装置と文字のフレームバッファ | 11 |
| [fs.md](fs.md) | ファイルシステム | 13 |
| [net.md](net.md) | ネットワーク | 25 |
| [settings.md](settings.md) | 設定 | 5 |
| [shell.md](shell.md) | シェル | 7 |
| [logging.md](logging.md) | ログ | 11 |
| [sys.md](sys.md) | システムのユーティリティ | 22 |
| [fff.md](fff.md) | 単体テストのモック (FFF) | 13 |

## 更新のしかた

コードで新しい Zephyr の API を使い始めたら、該当のファイルに、同じ形式で足す (見出し、シグネチャ、説明、使用箇所)。Zephyr のバージョンを上げたら、ヘッダの Doxygen コメントを読み直して、変わったところを直す。
