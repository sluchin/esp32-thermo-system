# AWS IoT Core への接続の設定

Thermo Gateway は、BLE で受信した温度と湿度と気圧を、WiFi と MQTT (TLS、クライアント証明書による相互認証) で、AWS IoT Core に送ります。このドキュメントは、AWS 側の準備と、ゲートウェイの設定の手順を説明します。

## 1. 送信されるデータ

共通: ポート 8883 (MQTT over TLS)、QoS 1。クライアント ID は、`thermo set client_id` で設定した値 (例: `gateway-01`)。

### 1.1 Thermo ノードの温度

| 項目 | 内容 |
|:---|:---|
| トピック | `thermo/CLIENT_ID/NODE_ADDRESS/temperature` (例: `thermo/gateway-01/00:AA:01:00:00:42/temperature`) |
| ペイロード | `{"node":"00:AA:01:00:00:42","type":"thermo","temperature_c":23.5,"humidity":45.0,"pressure_hpa":1013.2,"uptime_ms":123456,"timestamp":1790000000}` |

- `temperature_c` は温度 [℃]、`humidity` は湿度 [%]、`pressure_hpa` は気圧 [hPa] で、どれも小数第 1 位までです。湿度を測れないセンサ (ADC のアナログセンサなど) のときは、`humidity` を出力しません。気圧を測れないセンサ (DHT11、ADC) のときと、気圧のセンサ (BMP280) が使えないときは、`pressure_hpa` を出力しません。キー名は SwitchBot と同じです (`type` だけが `thermo` です。SwitchBot には、`pressure_hpa` はありません)。
- 気圧を足す前のノード (温度と湿度だけを BLE で送る) も、ゲートウェイは受信して、`pressure_hpa` のないメッセージを送ります。AWS 側の設定は、8 章です。
- `uptime_ms` は、ゲートウェイが温度を受信したときの、ゲートウェイの稼働時間 [ms] です。
- `timestamp` は、ゲートウェイが受信したときの UNIX 時刻 [s] です (SwitchBot も同じ)。SNTP で時計が合っているときだけ入ります (MQTT に接続する前に必ず時計を合わせるので、通常は入ります)。

### 1.2 SwitchBot 屋外用温湿度計 (Outdoor Meter)

SwitchBot とは接続せずに、アドバタイズ (広告パケット) で受信した値を、1 台につき 10 秒に 1 回 (`prj.conf` の `CONFIG_THERMO_SWITCHBOT_INTERVAL_MS=10000`) 送ります。

| 項目 | 内容 |
|:---|:---|
| トピック | `thermo/CLIENT_ID/switchbot/DEVICE_ADDRESS` (例: `thermo/gateway-01/switchbot/B0:E9:FE:12:34:56`) |
| ペイロード | `{"node":"B0:E9:FE:12:34:56","type":"switchbot","temperature_c":23.5,"humidity":55,"battery":87,"uptime_ms":123456,"timestamp":1790000000}` |

- 温度 `temperature_c` の単位は ℃、湿度 `humidity` と電池残量 `battery` の単位は % です。電池残量がわからないときは、`battery` を出力しません。
- ポリシーの `topic/thermo/gateway-01/*` には、このトピックも含まれるので、ポリシーの変更は不要です。
- 10 秒に 1 回だと、1 台で 1 日に 8,640 件、1 か月に約 26 万件です。AWS IoT Core は、メッセージの件数で課金されます (料金は、リージョンごとの料金表で確認してください)。間隔は、`prj.conf` の `CONFIG_THERMO_SWITCHBOT_INTERVAL_MS` (ミリ秒) で変えられます (Kconfig の既定値は 1000)。
- アドバタイズのデータの並びは、SwitchBot の公開仕様に基づいています。機種コードが屋外用温湿度計と違う機種や、温度の位置が違う機種では、値が出ません。

## 2. AWS 側の準備

AWS CLI の例です (リージョンは、自分のものに置き換えます。アカウント ID は、コマンドで取り出します)。コンソールからも、同じ設定ができます。

```bash
# 共通の変数 (同じシェルで、続けて実行する。アカウント ID は、コマンドで取り出して、書き写さない)
export AWS_REGION=ap-northeast-1
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)

# モノ (デバイス) の登録
aws iot create-thing --thing-name gateway-01

# 証明書と鍵の作成 (有効化する)。device.pem.crt と private.pem.key が、ゲートウェイに登録するものです
CERT_ARN=$(aws iot create-keys-and-certificate --set-as-active \
  --certificate-pem-outfile device.pem.crt \
  --public-key-outfile public.pem.key \
  --private-key-outfile private.pem.key \
  --query certificateArn --output text)

# ルート CA 証明書 (サーバの確認用)
curl -o AmazonRootCA1.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem

# ポリシー: このクライアント ID での接続と、このゲートウェイのトピックへの publish だけを許す
cat > policy.json <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:Connect",
      "Resource": "arn:aws:iot:${AWS_REGION}:${ACCOUNT_ID}:client/gateway-01"
    },
    {
      "Effect": "Allow",
      "Action": "iot:Publish",
      "Resource": "arn:aws:iot:${AWS_REGION}:${ACCOUNT_ID}:topic/thermo/gateway-01/*"
    }
  ]
}
JSON
aws iot create-policy --policy-name thermo-gateway --policy-document file://policy.json
aws iot attach-policy --policy-name thermo-gateway --target "$CERT_ARN"
aws iot attach-thing-principal --thing-name gateway-01 --principal "$CERT_ARN"

# エンドポイント (ATS)。表示された値を、thermo set endpoint に設定します
aws iot describe-endpoint --endpoint-type iot:Data-ATS
```

クライアント ID (`gateway-01`) は、ポリシーの `client/` と、ゲートウェイの `client_id` と、トピックの `thermo/gateway-01/` で、同じにします。

## 3. ゲートウェイの設定

ゲートウェイのシリアルコンソール (シェル) に、次のコマンドを入力します。設定は、フラッシュ (`storage_partition`) に保存されるので、再起動しても残ります。

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

1 つの PEM ごとに、次の 3 つの操作をします。最後の行の `CA` は、種類に合わせて `CLIENT` か `PK` に変えます。

```
cred buf load
(PEM を、`-----BEGIN` から `-----END` の行まで貼り付ける。最後に Ctrl-C)
cred add 1 CA default strt
```

- `cred buf load` は、貼り付けた文字を、改行も含めて、そのままバッファに入れます。`cred buf <1 行>` は改行を入れず、PEM が壊れるので、使いません。
- `Ctrl-C` を押して、`Stored N bytes.` と表示されれば、バッファに入っています。
- `strt` は、データを文字列として扱い、末尾に NUL を付ける指定です (mbedTLS が PEM を読むのに必要です)。
- バッファの大きさは、`CONFIG_TLS_CREDENTIALS_SHELL_CRED_BUF_SIZE` の 2048 バイトです。PEM がこれより長いと、エラーになります。
- シェルの受信バッファが小さい (64 バイト) ので、PEM を手で貼り付けると、文字が欠けて、`RX ring buffer full` と表示されます。貼り付けずに、次のスクリプトで、PC から少しずつ送ってください。

#### スクリプトで送る (推奨)

`scripts/send-cred.sh` は、`cred buf load` から `cred add` までの操作を、PEM の各行を 16 文字ずつに分けて送ります (1 つの PEM で、約 1 分かかります)。最後に `thermo save-certs` も送るので、証明書はフラッシュに保存されます (保存しないと、リセットやファームウェアの書き込みで消えます)。`picocom` などでポートを開いていると送れないので、閉じてください。

```bash
scripts/send-cred.sh CA     AmazonRootCA1.pem /dev/ttyACM0
scripts/send-cred.sh CLIENT device.pem.crt    /dev/ttyACM0
scripts/send-cred.sh PK     private.pem.key   /dev/ttyACM0
```

- 出力の `Stored N bytes.` の N が、`wc -c <ファイル>` の値と同じか、確認してください。
- 3 つが揃うまでは、`thermo save-certs` が `Failed to save the certificates` と表示します (登録していない種類があるため)。CA、CLIENT、PK の順に送れば、登録済みの分は保存されます。3 つ目を送ったあとに `certificates saved` と表示されれば、全て保存できています。
- PEM の改行は、LF だけにします (JSON の `\n` は、`jq -r` で改行に直します)。CR が混ざると、mbedTLS が読めません。
- 証明書と秘密鍵は、リポジトリに置いたままにしません (コミットしません)。

登録できたか確認します。

```
cred list
thermo show
```

登録できたら、反映します (`thermo save-certs` は、スクリプトが実行済みです。手で `cred add` したときは、自分で実行します)。

```
thermo apply
```

- `thermo show` は、設定の内容と、接続の準備ができているか (`ready to connect: yes`) を表示します (パスワードは `********` で伏せます)。
- `thermo apply` は、設定を反映するために、接続をやり直します。設定が揃っていなければ、ゲートウェイは、5 秒ごとに確認して、揃うまで待ちます。
- 同じ種類の証明書を登録し直すときは、先に `cred del 1 TYPE` (TYPE は、CA、CLIENT、PK) で消します。全部消すときは、`thermo reset` (設定と証明書を、フラッシュからも消します) を使います。

## 4. 動作の確認

(届いたデータをグラフにするには、[AWS_GRAPH.md](AWS_GRAPH.md) を参照してください。)

1. AWS コンソールの「IoT Core → MQTT テストクライアント」で、`thermo/#` を購読します。
2. ゲートウェイとノードを起動します。
3. ゲートウェイのログに、次の例のように表示されれば、接続できています。
4. MQTT テストクライアントに、5 秒ごとに、温度のメッセージが届きます。

ゲートウェイのログの例:

```
<inf> thermo_wifi: WiFi connected (IPv4 address acquired)
<inf> thermo_cloud: Connected to AWS IoT Core
<inf> thermo_gateway: Node 00:AA:01:00:00:42 (public): 23.5 C, humidity: 45.0 %
<inf> thermo_gateway: Node 00:AA:01:00:00:42 (public): pressure: 1013.2 hPa
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

接続に失敗したときは、5 秒、10 秒、20 秒、40 秒、60 秒 (上限) の間隔で、つなぎ直します。

### 5.1 TLS の設定 (実機で調整したもの)

AWS IoT Core の相互認証のために、`prj.conf` に、次の設定を入れています。

- `CONFIG_MBEDTLS_PEM_CERTIFICATE_FORMAT`: PEM の証明書と鍵を読む。
- ECDHE-RSA と AES-GCM を有効にする (`CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED` など)。AWS IoT Core の既定のセキュリティポリシーが、これを必要とするため。PSA 暗号の設定 (`CONFIG_PSA_WANT_*`) も、あわせて有効にする (RSA の署名、SHA-256、TLS 1.2 の PRF、ECDH、GCM)。足りないと、`-0x2700` (証明書の確認の失敗) や `-0x7F80` (鍵の導出の失敗) が出る。
- `CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=6144`: サーバの証明書のチェーンが約 5 KB ある。4096 では、`-0x7100` (`requesting more data than fits`) で失敗する。
- `CONFIG_NET_SOCKETS_CONNECT_TIMEOUT=30000`: ハンドシェイクの時間制限。
- デバッグ: `west build ... -- -DCONFIG_NET_LOG=y -DCONFIG_NET_SOCKETS_LOG_LEVEL_ERR=y -DCONFIG_MBEDTLS_DEBUG=y -DCONFIG_MBEDTLS_LOG_LEVEL_ERR=y` でビルドすると、TLS のエラーの詳細がログに出る。ログを全て (DBG) 出すと、スタックが足りなくなって、クラッシュすることがある。

## 6. 制限事項

- サーバ証明書の有効期限、署名、ホスト名を確認します。有効期限の確認には正しい時刻が要るので、WiFi に接続したあと、SNTP で時刻を合わせてから、MQTT に接続します。時刻を合わせられない間 (NTP サーバに届かないときなど) は、MQTT に接続せずに、つなぎ直します (ログの `Connection failed (err -62)`)。NTP サーバは、`CONFIG_THERMO_NTP_SERVER` で変えられます。
- 送信できなかった値は、キュー (16 件) があふれると、捨てます。再送はしません (最新の値を優先します)。
- ゲートウェイの RAM は、ほぼ使い切っています (`dram0_0_seg` が約 98%)。機能を足すときは、`CONFIG_MBEDTLS_HEAP_SIZE` など、ほかを減らす必要があります。
- `native_sim` のビルドは、ネットワークがないので、クラウドへの送信を含みません (`CONFIG_THERMO_CLOUD=n`)。

## 7. Thermo ノードから、直接送る (ゲートウェイがないとき)

ノードが、ゲートウェイにつながらないときに、WiFi で AWS IoT Core に直接送る機能 ([SETUP.md](SETUP.md) の「ゲートウェイにつながらないときの、WiFi での直接の送信と、SD カードへの保存」) のための、AWS 側の準備です。**ノードごとに、別のモノ、証明書、ポリシー、クライアント ID** を作ります (ゲートウェイとは共有しません)。

**ゲートウェイの証明書を、ノードに流用しないでください。** ゲートウェイと共有してよいものと、してはいけないものは、次のとおりです。

| もの | ゲートウェイと共有するか | 理由 |
|:---|:---|:---|
| ルート CA (`AmazonRootCA1.pem`) | **共有してよい** | AWS が公開している証明書で、秘密ではない。サーバを確認するためだけに使う |
| エンドポイント | 共有してよい | AWS アカウントごとに 1 つしかない |
| クライアント証明書、秘密鍵 | **共有しない** | 1 台の鍵が漏れたとき、その 1 台だけを無効にできるようにするため。共有すると、無効にしたときに全部の機器が止まる |
| クライアント ID | **共有しない** | 同じクライアント ID で 2 つ目の接続が来ると、AWS IoT Core は先の接続を切る。ゲートウェイとノードが奪い合って、どちらも、接続と切断を繰り返す |
| ポリシー | **共有しない** | ポリシーが、接続を許すクライアント ID と、publish を許すトピック (`thermo/<クライアント ID>/*`) を決めているため、別のクライアント ID では拒否される |
| モノ (thing) | 共有しない | 機器ごとに 1 つ作る |

```bash
# 共通の変数 (2 章と同じ。シェルを閉じたら、設定し直す)
export AWS_REGION=ap-northeast-1
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
NODE_ID=node-01        # ノードの名前 (クライアント ID。トピックの 2 つ目。ノードごとに変える)

# モノの登録と、証明書と鍵の作成 (ノードごとに、別のファイル名にする)
aws iot create-thing --thing-name "$NODE_ID"
CERT_ARN=$(aws iot create-keys-and-certificate --set-as-active \
  --certificate-pem-outfile "${NODE_ID}.pem.crt" \
  --public-key-outfile "${NODE_ID}.public.key" \
  --private-key-outfile "${NODE_ID}.private.key" \
  --query certificateArn --output text)

# ポリシー: このクライアント ID での接続と、このノードのトピックへの publish だけを許す
cat > policy-node.json <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "iot:Connect",
      "Resource": "arn:aws:iot:${AWS_REGION}:${ACCOUNT_ID}:client/${NODE_ID}"
    },
    {
      "Effect": "Allow",
      "Action": "iot:Publish",
      "Resource": "arn:aws:iot:${AWS_REGION}:${ACCOUNT_ID}:topic/thermo/${NODE_ID}/*"
    }
  ]
}
JSON
aws iot create-policy --policy-name "thermo-${NODE_ID}" --policy-document file://policy-node.json
aws iot attach-policy --policy-name "thermo-${NODE_ID}" --target "$CERT_ARN"
aws iot attach-thing-principal --thing-name "$NODE_ID" --principal "$CERT_ARN"
```

- 作った `${NODE_ID}.pem.crt` と `${NODE_ID}.private.key`、およびルート CA (`AmazonRootCA1.pem`。2 章) を、ノードのシェルに登録します ([SETUP.md](SETUP.md)。`scripts/send-cred.sh`)。クライアント ID は、`thermo set client_id ${NODE_ID}` で設定します (ポリシーの `client/` と、トピックの `thermo/${NODE_ID}/` と、同じにします)。
- エンドポイントは、ゲートウェイと同じです (`aws iot describe-endpoint --endpoint-type iot:Data-ATS`)。
- トピックは、`thermo/${NODE_ID}/<ノードの BLE アドレス>/temperature` で、ゲートウェイが送る形と同じです。そのため、DynamoDB のルール ([AWS_DYNAMODB.md](AWS_DYNAMODB.md)。`FROM 'thermo/#'`) と、thermon の機器の指定 (ノードの BLE アドレス) は、そのまま使えます。
- 作ったファイル (`*.pem.crt`、`*.private.key`、`policy-node.json`) は、**コミットしません**。秘密鍵は、ノードのフラッシュにだけ置きます。
- 1 台の証明書を無効にするとき: `aws iot update-certificate --certificate-id "$CERT_ID" --new-status INACTIVE` (`CERT_ID` は、証明書の ID。`aws iot list-certificates` で調べる)。

## 8. 気圧を送るときの AWS の設定

ノードに気圧センサ (AHT20 + BMP280 のモジュール。[SETUP.md](SETUP.md) の「Thermo ノードのセンサ」) を付けると、メッセージに `pressure_hpa` (気圧 [hPa]) が増えます (1.1)。トピックは変わりません。AWS 側で要るもの、要らないものは、次のとおりです。

| 設定 | 変更 | 理由 |
|:---|:---|:---|
| モノ、証明書、ポリシー (2 章、7 章) | **不要** | トピック (`thermo/CLIENT_ID/NODE_ADDRESS/temperature`) が同じなので、ポリシーの `iot:Publish` の範囲に入る |
| DynamoDB のルール ([AWS_DYNAMODB.md](AWS_DYNAMODB.md) の 4 章) | **不要** | `SELECT *` で、全ての項目を渡すので、`pressure_hpa` も、属性として保存される |
| DynamoDB のテーブル、Cognito の未認証ロール | **不要** | DynamoDB は、キー (`node`、`ts`) のほかの属性を、決めずに保存する。ロールの権限 (`dynamodb:Query`) も、属性によらない |
| CloudWatch のノードのルール ([AWS_GRAPH.md](AWS_GRAPH.md) の 7.2) | **要る** (グラフにするとき) | ルールの SQL が、項目の名前 (`temperature_c, humidity`) を選んでいるため。気圧のアクションを足す |
| Android アプリ (thermon) | アプリの更新 | 気圧のグラフと、最新の値の表示 (DynamoDB の `pressure_hpa` を読む) |

### 8.1 気圧が届いているか確認する

1. 「IoT Core → MQTT テストクライアント」で、`thermo/#` を購読します。
2. ノードのメッセージに、`"pressure_hpa":1013.2` のような項目があれば、届いています。ない場合は、ゲートウェイのログに `pressure:` の行があるかを確認します (ない場合は、ノードの起動のログの `Pressure sensor is not ready` を見ます。[SETUP.md](SETUP.md))。
3. DynamoDB に保存されたかは、ノードを指定して、最新の 1 件を取り出して確かめます (`$TABLE` は [AWS_DYNAMODB.md](AWS_DYNAMODB.md) の 2.2)。

```bash
aws dynamodb query --table-name "$TABLE" \
  --key-condition-expression "#n = :n" \
  --expression-attribute-names '{"#n":"node"}' \
  --expression-attribute-values '{":n":{"S":"00:AA:01:00:00:42"}}' \
  --no-scan-index-forward --limit 1 \
  --query 'Items[0].pressure_hpa'
```

`{"N": "1013.2"}` のように表示されれば、保存されています (`null` のときは、まだ気圧のない項目です)。

### 8.2 CloudWatch のグラフに、気圧を足す

すでにノードのルール (`thermo_node_to_cloudwatch`) を作ってあるときは、[AWS_GRAPH.md](AWS_GRAPH.md) の 7.2 の `rule-node.json` (気圧のアクションを足したもの) で、ルールを置き換えます。

```bash
aws iot replace-topic-rule --rule-name thermo_node_to_cloudwatch \
  --topic-rule-payload file://rule-node.json
```

まだ作っていないときは、7.2 の `create-topic-rule` で作ります。メトリクスの名前は、`00:AA:01:00:00:42_pressure_hpa` (ノードのアドレス + `_pressure_hpa`) です。
