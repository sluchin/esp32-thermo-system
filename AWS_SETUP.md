# AWS IoT Core への接続の設定

Thermo Gateway は、BLE で受信した温度 (ADC の生値) を、WiFi と MQTT (TLS、クライアント証明書による相互認証) で、AWS IoT Core に送ります。このドキュメントは、AWS 側の準備と、ゲートウェイの設定の手順です。

> **注意**: この機能は、単体テスト (モック) とビルドまでを確認しています。実機と AWS IoT Core には、まだつないでいません (確認する項目は [TODO.md](TODO.md))。手順の中の、`cred` コマンドの入力方法と、TLS のメモリ設定は、実機で調整が必要になるかもしれません。

## 1. 送信されるデータ

| 項目 | 内容 |
|:---|:---|
| ポート | 8883 (MQTT over TLS) |
| クライアント ID | `thermo set client_id` で設定した値 (例: `gateway-01`) |
| トピック | `thermo/CLIENT_ID/NODE_ADDRESS/temperature` (例: `thermo/gateway-01/00:AA:01:00:00:42/temperature`) |
| QoS | 1 |
| ペイロード | `{"node":"00:AA:01:00:00:42","raw":2568,"uptime_ms":123456}` |

- `raw` は、ノードの ADC の生値 (0〜4095) です。温度 (℃) への変換は、していません。
- `uptime_ms` は、ゲートウェイが温度を受信したときの、ゲートウェイの稼働時間です (時刻は、まだ入っていません)。

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

```
cred buf <PEM の 1 行目>
cred buf <PEM の 2 行目>
...
cred add 1 CA STRING
```

`cred buf` と `cred add` の書式は、ファームウェアの `cred add -h` と `cred buf -h` で確認してください (Zephyr の `tls_credentials_shell` の仕様です)。登録できたか確認します。

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
| `MQTT connect failed (err -...)` | TLS のハンドシェイクの失敗。証明書の登録 (`cred list`)、ルート CA の種類 (`AmazonRootCA1.pem` か)、エンドポイントが ATS か、を確認する。メモリ不足 (`-ENOMEM` など) なら、`prj.conf` の `CONFIG_MBEDTLS_HEAP_SIZE` を増やす (RAM に余裕がないので、ほかを減らす必要がある) |
| `MQTT CONNACK not received` / `MQTT connection refused` | ポリシーが、クライアント ID と、証明書に、合っていない。AWS IoT の「モニタリング → ログ」で、拒否の理由を確認する |
| 接続できているが、メッセージが届かない | トピックが、ポリシーの `topic/thermo/gateway-01/*` に合っているか確認する (`client_id` を変えたら、ポリシーも変える) |
| 接続が、何度も切れる | WiFi の電波と、BLE との干渉を確認する。ログの `MQTT connection lost (err ...)` の値を見る |

失敗したときは、5 秒、10 秒、20 秒、40 秒、60 秒 (上限) の間隔で、つなぎ直します。

## 6. 制限事項

- **サーバ証明書の有効期限は、確認していません** (署名とホスト名は、確認します)。時刻を持っていないためです。SNTP で時刻を合わせる予定です ([TODO.md](TODO.md))。
- 送信できなかった温度は、キュー (16 件) があふれると、捨てます。再送はしません (最新の値を優先)。
- ゲートウェイの RAM は、ほぼ使い切っています (`dram0_0_seg` が 約 98%)。機能を足すときは、`CONFIG_MBEDTLS_HEAP_SIZE` など、ほかを減らす必要があります。
- `native_sim` のビルドは、ネットワークがないので、クラウドへの送信を含みません (`CONFIG_THERMO_CLOUD=n`)。
