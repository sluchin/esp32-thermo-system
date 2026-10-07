# AWS IoT Core への接続の設定

Thermo Gateway は、BLE で受信した温度 (ADC の生値) を、WiFi と MQTT (TLS、クライアント証明書による相互認証) で、AWS IoT Core に送ります。このドキュメントは、AWS 側の準備と、ゲートウェイの設定の手順です。

> **注意**: 実機 (XIAO ESP32C3、外付けアンテナあり) で確認したのは、**SwitchBot の値の送信** (1.2) までです。WiFi、SNTP、TLS (相互認証)、MQTT の接続と、AWS IoT Core にメッセージが届くことを、確認しました。Thermo ノードの実機がないので、ノードの温度の送信 (1.1) は、単体テストだけで、実機では未確認です ([TODO.md](TODO.md))。

## 1. 送信されるデータ

共通: ポート 8883 (MQTT over TLS)、QoS 1。クライアント ID は、`thermo set client_id` で設定した値 (例: `gateway-01`)。

### 1.1 Thermo ノードの温度

> Thermo ノードの実機がないので、この経路は、実機では未確認です (単体テストのみ)。

| 項目 | 内容 |
|:---|:---|
| トピック | `thermo/CLIENT_ID/NODE_ADDRESS/temperature` (例: `thermo/gateway-01/00:AA:01:00:00:42/temperature`) |
| ペイロード | `{"node":"00:AA:01:00:00:42","raw":2568,"uptime_ms":123456,"timestamp":1790000000}` |

- `raw` は、ノードの ADC の生値 (0〜4095) です。温度 (℃) への変換は、していません。
- `uptime_ms` は、ゲートウェイが温度を受信したときの、ゲートウェイの稼働時間です。
- `timestamp` は、受信したときの UNIX 時刻 [s] です (SwitchBot も同じ)。SNTP で時計が合っているときだけ入ります (MQTT に接続する前に、必ず合わせるので、通常は入ります)。

### 1.2 SwitchBot 屋外用温湿度計 (Outdoor Meter)

接続せずに、アドバタイズから受信した値を、10 秒に 1 回 (1 台あたり。`prj.conf` の `CONFIG_THERMO_SWITCHBOT_INTERVAL_MS=10000`) 送ります。

| 項目 | 内容 |
|:---|:---|
| トピック | `thermo/CLIENT_ID/switchbot/DEVICE_ADDRESS` (例: `thermo/gateway-01/switchbot/B0:E9:FE:12:34:56`) |
| ペイロード | `{"node":"B0:E9:FE:12:34:56","type":"switchbot","temperature_c":23.5,"humidity":55,"battery":87,"uptime_ms":123456,"timestamp":1790000000}` |

- 温度は、機器が変換した ℃ です (Thermo ノードの `raw` とは違います)。電池残量がわからないときは、`battery` を出力しません。
- ポリシーの `topic/thermo/gateway-01/*` に、このトピックも含まれます (変更は、不要です)。
- 10 秒に 1 回だと、1 台で 1 日に 8,640 件、1 か月に約 26 万件です。AWS IoT Core は、メッセージの件数で課金されます (料金は、リージョンごとの料金表で確認してください)。間隔は、`prj.conf` の `CONFIG_THERMO_SWITCHBOT_INTERVAL_MS` (ミリ秒) で変えられます (Kconfig の既定値は 1000)。
- アドバタイズの並びは、SwitchBot の公開仕様に基づいています。実機 (屋外用温湿度計) で、温度・湿度・電池残量が、SwitchBot のアプリの表示と一致することと、AWS IoT Core に届くことを確認しました。屋外用温湿度計の機種コードと、温度の位置が違う機種では、値が出ません。

## 2. AWS 側の準備

AWS CLI の例です (リージョンとアカウント ID は、自分のものに置き換えます)。コンソールからも、同じ設定ができます。

```bash
# モノ (デバイス) の登録
aws iot create-thing --thing-name gateway-01

# 証明書と鍵の作成 (有効化する)。device.pem.crt と private.pem.key が、ゲートウェイに登録するものです
aws iot create-keys-and-certificate --set-as-active \
  --certificate-pem-outfile device.pem.crt \
  --public-key-outfile public.pem.key \
  --private-key-outfile private.pem.key
# 出力の certificateArn を、次の手順で使います

# ルート CA 証明書 (サーバの確認用)
curl -o AmazonRootCA1.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem

# ポリシー: このクライアント ID での接続と、このゲートウェイのトピックへの publish だけを許す
cat > policy.json <<'JSON'
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:Connect",
      "Resource": "arn:aws:iot:REGION:ACCOUNT_ID:client/gateway-01"
    },
    {
      "Effect": "Allow",
      "Action": "iot:Publish",
      "Resource": "arn:aws:iot:REGION:ACCOUNT_ID:topic/thermo/gateway-01/*"
    }
  ]
}
JSON
aws iot create-policy --policy-name thermo-gateway --policy-document file://policy.json
aws iot attach-policy --policy-name thermo-gateway --target <certificateArn>
aws iot attach-thing-principal --thing-name gateway-01 --principal <certificateArn>

# エンドポイント (ATS)。thermo set endpoint に設定します
aws iot describe-endpoint --endpoint-type iot:Data-ATS
```

クライアント ID (`gateway-01`) は、ポリシーの `client/` と、ゲートウェイの `client_id` と、トピックの `thermo/gateway-01/` で、同じにします。

## 3. ゲートウェイの設定

ゲートウェイのシリアルコンソール (シェル) に、次のコマンドを入力します。設定は、フラッシュ (`storage_partition`) に保存されて、再起動しても残ります。

### 3.1 WiFi と AWS IoT Core

```
thermo set ssid <SSID>
thermo set psk <パスワード>
thermo set endpoint xxxxxxxxxxxxxx-ats.iot.<リージョン>.amazonaws.com
thermo set client_id gateway-01
```

- 空白を含む値は、ダブルクォートで囲みます (例: `thermo set ssid "my home ap"`)。
- オープンネットワーク (パスワードなし) の場合は、`thermo set psk ""` にします。

### 3.2 証明書 (3 つ)

Zephyr の `cred` コマンドで、セキュリティタグ `1` に、3 つの PEM を登録して、`thermo save-certs` で、フラッシュに保存します。

| 種類 | `cred add` の種類 | ファイル |
|:---|:---|:---|
| ルート CA | `CA` | `AmazonRootCA1.pem` |
| クライアント証明書 | `CLIENT` | `device.pem.crt` |
| 秘密鍵 | `PK` | `private.pem.key` |

1 つの証明書ごとに、次の 3 つを実行します (種類は、`CA` / `CLIENT` / `PK`)。

```
cred buf load
(PEM を、`-----BEGIN` から `-----END` の行まで貼り付ける。最後に Ctrl-C)
cred add 1 CA default strt
```

- `cred buf load` は、貼り付けた文字を、改行も含めて、そのままバッファに入れます。`cred buf <1 行>` は、改行を入れないので、PEM が壊れるため、使いません。
- `Ctrl-C` で、`Stored N bytes.` と出れば、バッファに入っています。
- `strt` は、文字列で、末尾に NUL を付ける指定です (mbedTLS が PEM を読むのに必要です)。
- バッファは、`CONFIG_TLS_CREDENTIALS_SHELL_CRED_BUF_SIZE` (2048 バイト) です。PEM が、これより長いと、エラーになります。
- シェルの受信バッファが小さい (64 バイト) ので、貼り付けると、文字が欠けて、`RX ring buffer full` と出ます。貼り付けず、次のスクリプトで、PC から、ゆっくり送ってください。

#### スクリプトで送る (推奨)

`scripts/send-cred.sh` が、`cred buf load` から `cred add` までを、1 行を 16 文字ずつに分けて、送ります (1 つの PEM で、約 1 分かかります)。`picocom` などで、ポートを開いたままにしないでください。

```bash
scripts/send-cred.sh CA     AmazonRootCA1.pem /dev/ttyACM0
scripts/send-cred.sh CLIENT device.pem.crt    /dev/ttyACM0
scripts/send-cred.sh PK     private.pem.key   /dev/ttyACM0
```

- 出力の `Stored N bytes.` の N が、`wc -c <ファイル>` の値と同じか、確認してください。
- 登録済みの種類は、先に `cred del 1 CA` (`CLIENT`、`PK`) で消します。
- PEM の改行は、LF だけにします (JSON の `\n` は、`jq -r` で、改行に直します)。CR が混ざると、mbedTLS が読めません。
- 証明書と秘密鍵は、リポジトリに置いたままにしません (コミットしない)。

登録できたか確認します。

```
cred list
thermo show
```

登録できたら、保存します。

```
thermo save-certs
thermo apply
```

- `thermo show` は、設定の状態と、`ready to connect: yes` を表示します (パスワードは、`********` で伏せます)。
- `thermo apply` は、設定を反映するために、接続をやり直します。設定が揃っていなければ、ゲートウェイは、5 秒ごとに確認して、揃うまで待ちます。
- 同じ種類の証明書を登録し直すときは、先に `cred del 1 TYPE` (TYPE は、CA、CLIENT、PK) で消します。全部消すときは、`thermo reset` (設定と証明書を、フラッシュからも消す) を使います。

## 4. 動作の確認

(届いたデータをグラフにするには、[AWS_GRAPH.md](AWS_GRAPH.md) を参照してください。)

1. AWS コンソールの「IoT Core → MQTT テストクライアント」で、`thermo/#` を購読します。
2. ゲートウェイと、ノードを起動します。
3. ゲートウェイのログに、次のように出れば、接続できています。
4. MQTT テストクライアントに、5 秒ごとに、温度のメッセージが届きます。

ゲートウェイのログの例:

```
<inf> thermo_wifi: WiFi connected (IPv4 address acquired)
<inf> thermo_cloud: Connected to AWS IoT Core
<inf> thermo_gateway: Temperature from 00:AA:01:00:00:42 (public): 2568 (raw ADC value)
```

## 5. うまくいかないとき

| 症状 | 原因と対処 |
|:---|:---|
| `WiFi connection failed` | SSID とパスワードを確認する。2.4 GHz のネットワークを使う (ESP32C3 は 5 GHz に対応していない) |
| `Resolving '...' failed` | エンドポイントの値を確認する (`thermo show`)。ネットワークが、インターネットにつながっているか確認する |
| `MQTT connect failed (err -116)` | TLS のハンドシェイクが、時間内に終わらない。ESP32C3 は、RSA と ECDH の計算に、数秒かかる (`prj.conf` の `CONFIG_NET_SOCKETS_CONNECT_TIMEOUT` は、30000 ms)。電波が弱いときも出る (外付けアンテナを付ける) |
| `MQTT connect failed (err -22)` | 証明書か鍵が、読めない。PEM の形式 (`CONFIG_MBEDTLS_PEM_CERTIFICATE_FORMAT`)、登録したサイズを確認する |
| `MQTT connect failed (err -...)` | TLS のハンドシェイクの失敗。証明書の登録 (`cred list`)、ルート CA の種類 (`AmazonRootCA1.pem` か)、エンドポイントが ATS か、を確認する。メモリ不足 (`-ENOMEM` など) なら、`prj.conf` の `CONFIG_MBEDTLS_HEAP_SIZE` を増やす (RAM に余裕がないので、ほかを減らす必要がある) |
| `MQTT CONNACK not received` / `MQTT connection refused` | ポリシーが、クライアント ID と、証明書に、合っていない。AWS IoT の「モニタリング → ログ」で、拒否の理由を確認する |
| 接続できているが、メッセージが届かない | トピックが、ポリシーの `topic/thermo/gateway-01/*` に合っているか確認する (`client_id` を変えたら、ポリシーも変える) |
| 接続が、何度も切れる | WiFi の電波と、BLE との干渉を確認する。ログの `MQTT connection lost (err ...)` の値を見る |

失敗したときは、5 秒、10 秒、20 秒、40 秒、60 秒 (上限) の間隔で、つなぎ直します。

### 5.1 TLS の設定 (実機で調整したもの)

AWS IoT Core の相互認証のために、`prj.conf` に、次の設定を入れています。

- `CONFIG_MBEDTLS_PEM_CERTIFICATE_FORMAT`: PEM の証明書と鍵を読む。
- ECDHE-RSA と AES-GCM (`CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED` など)。AWS IoT Core の既定のポリシーは、これが必要。PSA 暗号 (`CONFIG_PSA_WANT_*`) も、あわせて有効にする (RSA の署名、SHA-256、TLS 1.2 の PRF、ECDH、GCM)。足りないと、`-0x2700` (証明書の確認の失敗) や、`-0x7F80` (鍵の導出の失敗) が出る。
- `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=6144`: サーバの証明書のチェーンが、約 5 KB ある。4096 では、`-0x7100` (`requesting more data than fits`) で失敗する。
- `CONFIG_NET_SOCKETS_CONNECT_TIMEOUT=30000`: ハンドシェイクの時間制限。
- デバッグ: `west build ... -- -DCONFIG_NET_LOG=y -DCONFIG_NET_SOCKETS_LOG_LEVEL_ERR=y -DCONFIG_MBEDTLS_DEBUG=y -DCONFIG_MBEDTLS_LOG_LEVEL_ERR=y` で、TLS のエラーが出る。ログを全て (DBG) 出すと、スタックが足りなくなって、クラッシュすることがある。

## 6. 制限事項

- サーバ証明書の有効期限は、確認します (署名とホスト名も、確認します)。そのため、WiFi に接続したあと、SNTP で時刻を合わせてから、MQTT に接続します。時刻を合わせられない間 (NTP サーバに届かないときなど) は、MQTT に接続しないで、つなぎ直します (ログの `Connection failed (err -62)`)。NTP サーバは、`CONFIG_THERMO_NTP_SERVER` で変えられます。
- 送信できなかった値は、キュー (16 件) があふれると、捨てます。再送はしません (最新の値を優先)。
- ゲートウェイの RAM は、ほぼ使い切っています (`dram0_0_seg` が 約 98%)。機能を足すときは、`CONFIG_MBEDTLS_HEAP_SIZE` など、ほかを減らす必要があります。
- `native_sim` のビルドは、ネットワークがないので、クラウドへの送信を含みません (`CONFIG_THERMO_CLOUD=n`)。
