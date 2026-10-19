# AWS に届いたデータを DynamoDB に保存して、アプリから読む

AWS IoT Core に届いた温度・湿度・電池残量を、DynamoDB に 1 件ずつ保存して、Android アプリ ([thermon](https://github.com/sluchin/thermon)) から読む手順です。アプリは、Cognito の「未認証ロール」を使って、読み取りだけを許可されます (ログインはありません)。

> **注意**: この手順は、AWS のドキュメントに基づいています。AWS の画面、料金、サービスの提供状況は変わることがあるので、最新の情報を確認してください。
>
> Thermo ノードの温度 ([AWS_SETUP.md](AWS_SETUP.md) の 1.1) も、SwitchBot と同じ形のメッセージなので、同じルールで保存されます。

## 1. 構成

```
ゲートウェイ ──MQTT──> AWS IoT Core ──ルール──> DynamoDB (テーブル thermo-readings)
                          thermo/#                       ▲
                                                         │ Query (読み取りだけ)
thermon ──> Cognito ID プール (未認証) ──一時的な認証情報──┘
```

- **ルール**: 届いた MQTT のメッセージを SQL で絞り込み、別のサービスに渡す、IoT Core の機能です。DynamoDBv2 アクションは、メッセージの各項目を、そのままテーブルの属性として書き込みます。
- **Cognito ID プール**: アプリに、AWS の一時的な認証情報を渡す仕組みです。「未認証ロール」を有効にすると、ログインしなくても、決めた権限だけを持つ認証情報を受け取れます。
- 保存は、ルールの設定だけでできます。コードを書くのは、アプリだけです。

### 1.1 DynamoDB と Timestream

| 比べること | DynamoDB | Timestream |
|:---|:---|:---|
| 時系列の保存 | キーの設計で対応する (機器と時刻) | 時系列専用 |
| 設定の手間 | 少ない | 多い |
| 提供の状況 | 新しく始める場合も、利用できる | Timestream for LiveAnalytics は、2025 年 6 月から、新しい利用者を受け付けていない (最新の状況は、AWS で確認してください)。InfluxDB 版は、別のサービス |
| 向いている用途 | 機器と期間を指定して取り出す。個人の規模 | 複雑な集計、SQL での分析 |

このドキュメントは、**DynamoDB** を使って書きます。個人の規模 (1 台、10 秒に 1 件) なら、十分です。

### 1.2 制限

- **取り出し方**: 機器 (`node`) を指定して、時刻の範囲で取り出します。「全ての機器の、ある時刻のデータ」は、1 回では取り出せません (機器ごとに取り出します)。
- **集計**: 平均や最大値は、アプリ側で計算します (DynamoDB は集計しません)。
- **件数と料金**: 10 秒に 1 件だと、1 台で 1 日に 8,640 件、1 か月に約 26 万件です。書き込みの料金は、リクエストの数で決まります (オンデマンド)。保存する量が増え続けないように、古い項目を自動で削除します (2.1 参照)。料金は、リージョンごとの料金表で確認してください。

## 2. テーブルを作る

### 2.1 項目の設計

| 属性 | 型 | 内容 |
|:---|:---|:---|
| `node` | 文字列 | パーティションキー。機器の MAC アドレス (例: `ED:2E:C4:46:3B:11`)。メッセージの `node` の値 |
| `ts` | 数値 | ソートキー。IoT Core がメッセージを受信した時刻 (UNIX 時刻 [s])。ルールが付けます |
| `type` | 文字列 | 機器の種類。`switchbot` または `thermo` |
| `temperature_c` | 数値 | 温度 [℃] |
| `humidity` | 数値 | 湿度 [%]。湿度を測れないセンサの項目には、この属性は保存されません |
| `battery` | 数値 | 電池残量 [%]。SwitchBot だけ。電池残量がわからないときは、この属性は保存されません |
| `expires_at` | 数値 | この時刻 (UNIX 時刻 [s]) を過ぎると、項目が自動で削除されます (TTL。受信から 30 日後) |

- 表の属性のほかに、メッセージの `uptime_ms` と `timestamp` も、そのまま属性として保存されます (4.1 の `SELECT *`)。
- ソートキーの `ts` には、メッセージの `timestamp` (ゲートウェイの時計) ではなく、ルールの `timestamp()` (IoT Core の時計) を使います。ゲートウェイの時計が合っていないと、`timestamp` が入らないことがあるためです ([AWS_SETUP.md](AWS_SETUP.md))。
- `ts` は、秒単位です。同じ機器から同じ秒に 2 件届くと、上書きされます。ゲートウェイは、機器 1 台につき 10 秒に 1 回しか送らないので、通常は起きません。

### 2.2 作成する

以降のコマンドは、同じシェルで続けて実行します (シェルを閉じたら、次の変数を設定し直します)。アカウント ID (12 桁の数字) は、コマンドで取り出して変数に入れるので、ファイルや文書に書き写しません。

```bash
export AWS_REGION=ap-northeast-1
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
TABLE=thermo-readings
echo "$ACCOUNT_ID"
```

- 以降で作る JSON ファイルには、アカウント ID などが入ります。リポジトリの外 (`~/tmp` など) で実行すると、ファイルがリポジトリに入りません。リポジトリの直下で実行したときは、 `.gitignore` が無視します。**コミットしないでください。**
- アカウント ID は、パスワードではありませんが、公開する必要もありません。リポジトリには書かないでください。

AWS CLI の例です。

```bash
aws dynamodb create-table \
  --table-name "$TABLE" \
  --attribute-definitions AttributeName=node,AttributeType=S AttributeName=ts,AttributeType=N \
  --key-schema AttributeName=node,KeyType=HASH AttributeName=ts,KeyType=RANGE \
  --billing-mode PAY_PER_REQUEST

# 古い項目を自動で消す (属性 expires_at)
aws dynamodb update-time-to-live \
  --table-name "$TABLE" \
  --time-to-live-specification Enabled=true,AttributeName=expires_at
```

コンソールで作る場合は、「DynamoDB → テーブル → テーブルの作成」で、テーブル名は `thermo-readings`、パーティションキーは `node` (文字列)、ソートキーは `ts` (数値)、容量モードは「オンデマンド」にします。作成後に、「追加の設定」の「有効期限 (TTL)」を開いて、属性名に `expires_at` を指定します。

## 3. IoT Core が DynamoDB に書き込むための権限 (IAM ロール)

共通の変数は、2.2 のものを使います。ロールの種類は「カスタム信頼ポリシー」にします (「AWS のサービス」の「IoT」を選ぶと、不要な管理ポリシーが付きます)。

```bash
cat > iot-trust.json <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": { "Service": "iot.amazonaws.com" },
      "Action": "sts:AssumeRole"
    }
  ]
}
JSON

cat > iot-put.json <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "dynamodb:PutItem",
      "Resource": "arn:aws:dynamodb:${AWS_REGION}:${ACCOUNT_ID}:table/${TABLE}"
    }
  ]
}
JSON

aws iam create-role --role-name thermo-iot-dynamodb \
  --assume-role-policy-document file://iot-trust.json
aws iam put-role-policy --role-name thermo-iot-dynamodb \
  --policy-name put-item --policy-document file://iot-put.json
```

ロールの ARN (`arn:aws:iam::アカウント ID:role/thermo-iot-dynamodb`) は、次の章のルールで、`${ACCOUNT_ID}` から組み立てます。

## 4. ルールを作る

### 4.1 SQL

```sql
SELECT *, floor(timestamp() / 1000) AS ts, floor(timestamp() / 1000) + 2592000 AS expires_at
FROM 'thermo/#'
WHERE isUndefined(temperature_c) = false
```

- `FROM 'thermo/#'` は、`thermo/` で始まる全てのトピックです (SwitchBot と Thermo ノードの両方)。
- `WHERE` で、`temperature_c` のないメッセージを除きます。
- `timestamp()` は、IoT Core が受信した時刻 [ミリ秒] です。`floor(... / 1000)` で秒にします。`2592000` は、30 日の秒数です。
- `SELECT *` は、メッセージの全ての項目 (`node`、`type`、`temperature_c`、`humidity`、`battery`、`uptime_ms`、`timestamp`) を、そのまま渡します。

### 4.2 ルールを作る

```bash
cat > rule-dynamodb.json <<JSON
{
  "sql": "SELECT *, floor(timestamp() / 1000) AS ts, floor(timestamp() / 1000) + 2592000 AS expires_at FROM 'thermo/#' WHERE isUndefined(temperature_c) = false",
  "awsIotSqlVersion": "2016-03-23",
  "ruleDisabled": false,
  "actions": [
    {
      "dynamoDBv2": {
        "roleArn": "arn:aws:iam::${ACCOUNT_ID}:role/thermo-iot-dynamodb",
        "putItem": { "tableName": "${TABLE}" }
      }
    }
  ]
}
JSON

aws iot create-topic-rule \
  --rule-name thermo_to_dynamodb --topic-rule-payload file://rule-dynamodb.json
```

ルール名に使えるのは、英数字とアンダースコアだけです。[AWS_GRAPH.md](AWS_GRAPH.md) の CloudWatch のルール (`thermo_switchbot_to_cloudwatch`) とは別のルールなので、同時に動かせます。

コンソールで作る場合は、「IoT Core → メッセージのルーティング → ルール → ルールを作成」で、SQL に 4.1 を貼ります。アクションは「DynamoDBv2」を選びます (「DynamoDB」は、メッセージ全体を 1 つの属性に入れるので、使いません。「DynamoDBv2」は、メッセージの各項目を別々の属性にします)。テーブルは `thermo-readings`、IAM ロールは `thermo-iot-dynamodb` です。

## 5. データが届いているか確認する

ゲートウェイを動かして、少し待ってから確認します。

```bash
# 保存された項目を 5 件取り出す (Scan は、少量のテーブルでの確認だけに使ってください)
aws dynamodb scan --table-name "$TABLE" --max-items 5

# 機器を指定して、最新の 3 件を取り出す
aws dynamodb query --table-name "$TABLE" \
  --key-condition-expression "#n = :n" \
  --expression-attribute-names '{"#n":"node"}' \
  --expression-attribute-values '{":n":{"S":"ED:2E:C4:46:3B:11"}}' \
  --no-scan-index-forward --limit 3
```

何も保存されないときは、IoT Core のログを有効にして、CloudWatch Logs を見ます。よくある原因は、ロールの権限不足 (`dynamodb:PutItem`)、テーブル名やリージョンの違い、SQL の結果にキーの `node` か `ts` がない場合です。

## 6. アプリ用の認証 (Cognito ID プール、未認証ロール)

### 6.1 ID プールを作る

```bash
IDENTITY_POOL_ID=$(aws cognito-identity create-identity-pool \
  --identity-pool-name thermo_client \
  --allow-unauthenticated-identities \
  --query IdentityPoolId --output text)
echo "$IDENTITY_POOL_ID"
```

ID プールの ID (`ap-northeast-1:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`) は、変数 `IDENTITY_POOL_ID` に入っています。アプリの設定ファイルに書く値です。リポジトリには書きません。

### 6.2 未認証ロール (読み取りだけ)

```bash
cat > unauth-trust.json <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": { "Federated": "cognito-identity.amazonaws.com" },
      "Action": "sts:AssumeRoleWithWebIdentity",
      "Condition": {
        "StringEquals": { "cognito-identity.amazonaws.com:aud": "${IDENTITY_POOL_ID}" },
        "ForAnyValue:StringLike": { "cognito-identity.amazonaws.com:amr": "unauthenticated" }
      }
    }
  ]
}
JSON

cat > unauth-read.json <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "dynamodb:Query",
      "Resource": "arn:aws:dynamodb:${AWS_REGION}:${ACCOUNT_ID}:table/${TABLE}"
    }
  ]
}
JSON

aws iam create-role --role-name thermo-client-unauth \
  --assume-role-policy-document file://unauth-trust.json
aws iam put-role-policy --role-name thermo-client-unauth \
  --policy-name read-thermo --policy-document file://unauth-read.json

# ID プールに、未認証ロールとして登録する
aws cognito-identity set-identity-pool-roles \
  --identity-pool-id "$IDENTITY_POOL_ID" \
  --roles "unauthenticated=arn:aws:iam::${ACCOUNT_ID}:role/thermo-client-unauth"
```

`IDENTITY_POOL_ID` は 6.1 の値、`ACCOUNT_ID` と `TABLE` は 2.2 の値です。シェルが変わったときは、設定し直します。

### 6.3 安全のために

未認証ロールの権限は、**ID プールの ID を知っている人なら、誰でも**使えます (ID は、アプリのファイルから取り出せます)。次の点を守ってください。

- 権限は、このテーブルに対する `dynamodb:Query` だけを付けます (上のポリシーのとおり)。書き込み、削除、ほかのサービスの権限は付けません。
- アクセスキーは、アプリに埋め込みません。必ず、Cognito の一時的な認証情報を使います。
- アプリをほかの人に配るときは、Cognito のユーザープールによるログインに替えます (今は自分だけが使うので、未認証ロールで進めます)。

## 7. Android アプリ (thermon) から読む

読み取りには、Android アプリ [thermon](https://github.com/sluchin/thermon) を使います (Kotlin。別のリポジトリです)。このドキュメントで作った Cognito の ID プールと DynamoDB のテーブルを、そのまま使います。

- アプリの `core` モジュールは、6.1 の ID プールから、未認証の ID と一時的な認証情報を受け取り (`CognitoUnauthenticatedCredentials`)、テーブルを `Query` で読みます (`DynamoDbReadings`)。アクセスキーは、アプリに入れません。
- 接続先は、thermon の `local.properties` (リポジトリに入れない) に書きます。

  ```properties
  thermon.region=ap-northeast-1
  thermon.identityPoolId=<6.1 の IdentityPoolId>
  thermon.table=thermo-readings
  thermon.nodes=<機器の MAC アドレス (コンマ区切り)>
  ```

  `thermon.nodes` には、テーブルの `node` と同じ MAC アドレスを書きます。
- ビルド、端末への転送、表示の確認は、thermon の [SETUP.md](https://github.com/sluchin/thermon/blob/main/SETUP.md) の「アプリを動かして確認する」を見てください。thermon の [AWS_DYNAMODB.md](https://github.com/sluchin/thermon/blob/main/AWS_DYNAMODB.md) にも、この文書の 6 章までと同じ AWS の手順が書いてあります (`cat` で JSON を作り、`aws` コマンドで設定する形です)。AWS CLI の準備は、[AWS_CLI.md](https://github.com/sluchin/thermon/blob/main/AWS_CLI.md) を見てください。
- 機器ごとの最新の測定値と、温度と湿度のグラフ (1 時間、6 時間、24 時間) を表示します。

## 8. これから

- 古いデータは、`expires_at` により、受信から 30 日後に削除されます。長く残したいときは、TTL の期間 (4.1 の `2592000`) を長くするか、S3 にエクスポートします。
- グラフが粗くてよいなら、[AWS_GRAPH.md](AWS_GRAPH.md) の CloudWatch も併用できます。
- 複数人で使うときは、Cognito のユーザープールによるログインに替えます。
