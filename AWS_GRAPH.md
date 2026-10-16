# AWS に届いたデータをグラフにする

AWS IoT Core に届いた SwitchBot の温度・湿度・電池残量を、CloudWatch のグラフ (ダッシュボード) にする手順です。

> **注意**: この手順は、AWS のドキュメントに基づいています。AWS の画面や料金は変わることがあるので、最新の情報を確認してください。
>
> Thermo ノードの温度と湿度は、「7.2 Thermo ノードの温度と湿度」で扱います。

## 1. 構成

```
ゲートウェイ ──MQTT──> AWS IoT Core ──ルール──> CloudWatch メトリクス ──> ダッシュボード (グラフ)
                          thermo/+/switchbot/+      名前空間 Thermo
```

- **ルール**: 届いた MQTT のメッセージを SQL で絞り込み、別のサービスに渡す、IoT Core の機能です。
- **CloudWatch メトリクス**: 時間とともに変わる値 (時系列) を保存する仕組みです。保存した値から、そのままグラフを作れます。
- コードを書く必要はありません。

### 1.1 この構成にした理由と、制限

| 項目 | 内容 |
|:---|:---|
| よい点 | 設定だけで済む。ダッシュボードをすぐ使える。アラーム (しきい値を超えたら通知) も、同じ仕組みで作れる |
| 保存期間 | 1 分間隔のデータは 15 日、5 分間隔のデータは 63 日、1 時間間隔のデータは 455 日 (古いデータは、間隔が粗くなって残る) |
| 制限 | ルールからはディメンションを付けられないので、メトリクスの名前が、機器ごとに別になる。そのため、グラフとアラームの設定は、機器ごとに必要 |
| 向かないこと | 10 秒ごとの細かいデータを、そのまま残したいとき (1 分の平均になる)。複雑な集計や、SQL での分析 |

細かいデータや分析が必要なときは、「7.3 細かいデータを残す・分析する」の別の方法を検討してください。

## 2. 準備

### 2.1 用語: コンソール、CLI、CloudShell

この文書では、同じ設定を、「コンソール」と「CLI」の 2 つの方法で書いています。どちらか 1 つの方法で設定すれば十分です。

| 用語 | 意味 | この文書での使い方 |
|:---|:---|:---|
| **コンソール** (AWS マネジメントコンソール) | ブラウザで開く、AWS の画面 (GUI)。マウスで操作する | 各章の「コンソールで作る場合」(要点だけ) |
| **CLI** (AWS CLI) | コマンドで AWS を操作する道具。`aws ...` というコマンド | 各章の `bash` のコマンド |
| **CloudShell** | コンソールの中で開く、ブラウザ上のターミナル。**CLI が、はじめから入っていて、ログイン中のアカウントで、そのまま使える** | CLI のコマンドを、実行する場所の 1 つ |

- CLI は、CloudShell の別名ではありません。CloudShell は、CLI を実行できる場所の 1 つです。ほかに、自分のパソコンに AWS CLI を入れて実行する方法もあります (その場合は、アクセスキーなどの認証の設定が、別に必要です)。
- コンソールでできる操作は、ほとんど CLI でもできます (逆も同じです)。
- CloudShell は、コンソールの左下の「CloudShell」(または、上のバーのターミナルのアイコン) から開きます。リージョンは、コンソールで選んでいるリージョンになります。

### 2.2 準備すること

- AWS CLI が使えること (`aws sts get-caller-identity` で、アカウントを確認できます)。コンソールだけで設定することもできます (各手順に、コンソールでの操作を書きます)。
- ゲートウェイを [AWS_SETUP.md](AWS_SETUP.md) のとおりに設定して、SwitchBot の値が `thermo/gateway-01/switchbot/<MAC アドレス>` に届いていること。
- リージョンは、ゲートウェイと同じにします (例: `ap-northeast-1`)。以降のコマンドは、同じシェルで続けて実行します (シェルを閉じたら、次の変数を設定し直します)。アカウント ID (12 桁の数字) は、コマンドで取り出して変数に入れるので、ファイルや文書に書き写しません。

```bash
export AWS_REGION=ap-northeast-1
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
echo "$ACCOUNT_ID"
```

- 以降で作る JSON ファイルには、アカウント ID などが入ります。リポジトリの外 (`~/tmp` など) で実行すると、ファイルがリポジトリに入りません。リポジトリの直下で実行したときは、 `.gitignore` が無視します。**コミットしないでください。**

## 3. IoT Core が CloudWatch に書き込むための権限 (IAM ロール)

ルールは、このロールを使って、CloudWatch にメトリクスを書き込みます。

```bash
# 信頼ポリシー: IoT Core が、このロールを使える
cat > trust.json <<JSON
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

# 権限ポリシー: 名前空間 Thermo のメトリクスだけを書き込める
cat > perm.json <<JSON
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": "cloudwatch:PutMetricData",
      "Resource": "*",
      "Condition": {
        "StringEquals": { "cloudwatch:namespace": "Thermo" }
      }
    }
  ]
}
JSON

aws iam create-role --role-name thermo-iot-cloudwatch \
  --assume-role-policy-document file://trust.json
aws iam put-role-policy --role-name thermo-iot-cloudwatch \
  --policy-name put-metric --policy-document file://perm.json
```

ロールの ARN (`arn:aws:iam::アカウント ID:role/thermo-iot-cloudwatch`) は、次の章のルールで、`${ACCOUNT_ID}` から組み立てます。

### 3.1 コンソールで作る場合

CLI を使わずに、AWS コンソールで作るときの要点です (画面の名前は変わることがあります)。

1. 「IAM → ロール → ロールを作成」を開きます。
2. 信頼されたエンティティは、「**カスタム信頼ポリシー**」を選んで、上の `trust.json` の中身を貼ります。**「AWS のサービス」の「IoT」は、選びません** (選ぶと、このシステムでは不要な管理ポリシー `AWSIoTLogging`、`AWSIoTRuleActions`、`AWSIoTThingsRegistration` が、自動で付きます。付いてしまったときは、ロールの「許可」のタブで、この 3 つを外します)。
3. 「許可を追加」では、**何も選ばずに**次へ進みます。ロール名は `thermo-iot-cloudwatch` にします。
4. 作ったロールの「許可」のタブから、「インラインポリシーを作成」で、JSON に `perm.json` の中身を貼って、名前を `put-metric` にします。
5. ロールの「ARN」を控えます (CLI でルールを作るときの `roleArn` になります)。

## 4. ルールを作る

### 4.1 受け取る値

SwitchBot のメッセージ ([AWS_SETUP.md](AWS_SETUP.md) の 1.2) は、次の形です。

```json
{"node":"ED:2E:C4:46:3B:11","type":"switchbot","temperature_c":21.8,"humidity":54,"battery":100,"uptime_ms":223966,"timestamp":1791338131}
```

ルールの SQL で `temperature_c`、`humidity`、`battery` を取り出して、それぞれ別のメトリクスにします。

### 4.2 メトリクスの名前

CloudWatch のメトリクスは、「名前空間」(`Thermo`) と「メトリクス名」で決まります。ルールからは、ディメンション (機器を区別するための付加情報) を付けられないので、**機器の MAC アドレスを、メトリクス名に入れます**。

| メトリクス名 | 意味 |
|:---|:---|
| `ED:2E:C4:46:3B:11_temperature_c` | 温度 [℃] |
| `ED:2E:C4:46:3B:11_humidity` | 湿度 [%] |
| `ED:2E:C4:46:3B:11_battery` | 電池残量 [%] |

MAC アドレスは、トピックの 4 番目の要素 (`thermo/gateway-01/switchbot/ED:2E:...`) です。ルールでは、`${topic(4)}` で取り出します。

Thermo ノードの値も、同じ名前空間 `Thermo` に入れられます (手順は 7.2)。この場合は、ノードの BLE アドレスを、メトリクス名に入れます。

| メトリクス名 | 意味 |
|:---|:---|
| `00:AA:01:00:00:42_temperature_c` | ノードの温度 [℃] |
| `00:AA:01:00:00:42_humidity` | ノードの湿度 [%] (DHT11 のとき) |

### 4.3 ルールを作る

```bash
cat > rule.json <<JSON
{
  "sql": "SELECT temperature_c, humidity, battery FROM 'thermo/+/switchbot/+'",
  "awsIotSqlVersion": "2016-03-23",
  "ruleDisabled": false,
  "actions": [
    {
      "cloudwatchMetric": {
        "roleArn": "arn:aws:iam::${ACCOUNT_ID}:role/thermo-iot-cloudwatch",
        "metricNamespace": "Thermo",
        "metricName": "\${topic(4)}_temperature_c",
        "metricValue": "\${temperature_c}",
        "metricUnit": "None"
      }
    },
    {
      "cloudwatchMetric": {
        "roleArn": "arn:aws:iam::${ACCOUNT_ID}:role/thermo-iot-cloudwatch",
        "metricNamespace": "Thermo",
        "metricName": "\${topic(4)}_humidity",
        "metricValue": "\${humidity}",
        "metricUnit": "Percent"
      }
    },
    {
      "cloudwatchMetric": {
        "roleArn": "arn:aws:iam::${ACCOUNT_ID}:role/thermo-iot-cloudwatch",
        "metricNamespace": "Thermo",
        "metricName": "\${topic(4)}_battery",
        "metricValue": "\${battery}",
        "metricUnit": "Percent"
      }
    }
  ]
}
JSON

aws iot create-topic-rule --rule-name thermo_switchbot_to_cloudwatch \
  --topic-rule-payload file://rule.json
```

- JSON の中の `\${topic(4)}` の `\` は、シェルが `$` を展開しないための印です (ファイルには、`${topic(4)}` と書かれます)。`${ACCOUNT_ID}` は、シェルが、アカウント ID に置き換えます。
- `'thermo/+/switchbot/+'` の `+` は、1 つの階層に合うワイルドカードです。ゲートウェイの ID (`gateway-01`) にも、SwitchBot の MAC アドレスにも合います。
- `metricTimestamp` を指定しないと、IoT Core がメッセージを受け取った時刻が使われます (メッセージの `timestamp` は、ゲートウェイの時計が SNTP で合っていないと入らないので、使いません)。
- 電池残量がわからなくて、`battery` が出力されないときは、電池残量のアクションだけが実行されず、`battery` のメトリクスだけが欠けます。

### 4.4 コンソールで作る場合

CLI を使わずに、AWS コンソールで作るときの要点です。

1. 「IoT Core → メッセージのルーティング → ルール → ルールを作成」を開きます。リージョンは、ゲートウェイと同じ (例: 東京) にします。
2. ルール名は `thermo_switchbot_to_cloudwatch` です (英数字とアンダースコアだけ)。
3. SQL のバージョンは `2016-03-23`、SQL ステートメントは次の 1 行です。入力欄に貼ると、シングルクォート (`'`) が全角に変わることがあります。そのときは、半角に直します。

   ```sql
   SELECT temperature_c, humidity, battery FROM 'thermo/+/switchbot/+'
   ```

4. アクションは、「**CloudWatch metric**」を 3 つ追加します。入力欄は、次のとおりです。

   | 入力欄 | アクション 1 (温度) | アクション 2 (湿度) | アクション 3 (電池) |
   |:---|:---|:---|:---|
   | メトリクス名 | `${topic(4)}_temperature_c` | `${topic(4)}_humidity` | `${topic(4)}_battery` |
   | メトリクス名前空間 | `Thermo` | `Thermo` | `Thermo` |
   | 単位 | `None` | `Percent` | `Percent` |
   | 値 | `${temperature_c}` | `${humidity}` | `${battery}` |
   | IAM ロール | `thermo-iot-cloudwatch` | 同じ | 同じ |

   `${...}` は、そのまま入力します。**メトリクス名の前後に、空白を入れないでください** (先頭に空白が入ると、空白付きの名前のメトリクスができてしまいます)。直すときは、ルールのアクションの、メトリクス名を編集します。

5. 内容を確認して、「作成」を押します。ルールが「アクティブ」で、アクションが 3 つあれば、完了です。

## 5. データが届いているか確認する

1. ゲートウェイを起動して、1〜2 分待ちます (`Connected to AWS IoT Core` が、ログに出ていること)。
2. 次のコマンドで、メトリクスができたか確認します。

   ```bash
   aws cloudwatch list-metrics --namespace Thermo
   ```

   `ED:2E:C4:46:3B:11_temperature_c` など、3 つのメトリクスが表示されるはずです (最初の値が届いてから表示されるまで、数分かかることがあります)。

3. 直近の値を見ます。

   ```bash
   aws cloudwatch get-metric-statistics --namespace Thermo \
     --metric-name "ED:2E:C4:46:3B:11_temperature_c" \
     --start-time "$(date -u -d '30 minutes ago' +%Y-%m-%dT%H:%M:%SZ)" \
     --end-time "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
     --period 300 --statistics Average
   ```

- コンソール: 「CloudWatch → メトリクス → すべてのメトリクス」で、名前空間 `Thermo` を選びます。

## 6. グラフ (ダッシュボード) を作る

### 6.1 コンソールで作る

CLI を使わずに、AWS コンソールで作るときの要点です。

1. 「CloudWatch → ダッシュボード → ダッシュボードの作成」で、名前を `thermo` にします。
2. 「ウィジェットの追加」で、データソースタイプは「Cloudwatch」、データ型は「メトリクス」、ウィジェットのタイプは「**線**」を選びます。
3. 「カスタム名前空間」の「**Thermo**」→「ディメンションなしのメトリクス」で、メトリクスのチェックボックスをオンにして、「ウィジェットの作成」を押します。温度、湿度、電池残量を、1 つずつウィジェットにします (温度と湿度は単位が違うので、別のウィジェットにします)。一覧に同じ名前のメトリクスが 2 つ表示されたときは、名前の先頭に空白がないほうを選びます (8 章)。
4. 画面の右上の「**保存**」を押します (「自動保存」がオフのときは、変更のたびに押します。押さないと、ダッシュボードが保存されません)。

**凡例の名前を変える** (室外、室内など): ウィジェットの「⋮」→「編集」→「グラフ化したメトリクス」のタブで、「ラベル」の欄に `室外 (ED:2E:C4:46:3B:11)` のように入力して、「ウィジェットの更新」と「保存」を押します (CLI では、6.2 の `label` を使います)。

**期間と統計**: 何も変えなければ、統計は「平均」、期間は「5 分」です。変えるときは、ウィジェットの編集の「グラフ化したメトリクス」のタブで、メトリクスごとに選びます (ウィジェットの編集画面の右上にある「統計」と「期間」は、そのウィジェットの全てのメトリクスに、まとめて適用されます)。期間の最小は 1 分です。

**表示の範囲と時刻**: ダッシュボードの右上の `1時間` `3時間` `12時間` などのボタンで、表示する時間の範囲を変えます。「UTC タイムゾーン」を「ローカルタイムゾーン」に変えると、時刻が日本時間で表示されます。

### 6.2 CLI で作る

```bash
cat > dashboard.json <<JSON
{
  "widgets": [
    {
      "type": "metric", "x": 0, "y": 0, "width": 12, "height": 6,
      "properties": {
        "title": "Temperature [C]",
        "region": "ap-northeast-1",
        "metrics": [ [ "Thermo", "ED:2E:C4:46:3B:11_temperature_c", { "label": "室外 (ED:2E:C4:46:3B:11)" } ] ],
        "stat": "Average", "period": 300, "view": "timeSeries"
      }
    },
    {
      "type": "metric", "x": 12, "y": 0, "width": 12, "height": 6,
      "properties": {
        "title": "Humidity [%]",
        "region": "ap-northeast-1",
        "metrics": [ [ "Thermo", "ED:2E:C4:46:3B:11_humidity", { "label": "室外 (ED:2E:C4:46:3B:11)" } ] ],
        "stat": "Average", "period": 300, "view": "timeSeries"
      }
    },
    {
      "type": "metric", "x": 0, "y": 6, "width": 12, "height": 6,
      "properties": {
        "title": "Battery [%]",
        "region": "ap-northeast-1",
        "metrics": [ [ "Thermo", "ED:2E:C4:46:3B:11_battery", { "label": "室外 (ED:2E:C4:46:3B:11)" } ] ],
        "stat": "Average", "period": 3600, "view": "timeSeries"
      }
    }
  ]
}
JSON

aws cloudwatch put-dashboard --dashboard-name thermo \
  --dashboard-body file://dashboard.json
```

- `label` は、グラフの凡例に出す名前です。指定しないと、メトリクス名 (`ED:2E:C4:46:3B:11_temperature_c`) が、そのまま出ます。上の例は、ベランダに置いた SwitchBot を「室外」とし、MAC アドレスをカッコの中に出します。
- ノード (7.2) を足して、室内と室外を同じグラフに並べるときは、同じ `metrics` に行を足します。ラベルは、`{ "label": "室内 (00:AA:01:00:00:42)" }` のように付けます。
- 機器 (MAC アドレス) を足すときは、`metrics` に `[ "Thermo", "<MAC>_temperature_c" ]` の行を足します。ルールは、変えなくて済みます (`+` が、全ての機器に合うため)。
- `period` の単位は、秒です。10 秒ごとにデータが届いても、CloudWatch は 1 分単位で集計するので、`period` の最小は 60 です。

## 7. これから

### 7.1 アラーム: 温度が 30 ℃ を超えたら、メールで通知する

CloudWatch の「アラーム」は、メトリクスの値が、しきい値を超えたときに、通知を送る仕組みです。ここでは、`ED:2E:C4:46:3B:11_temperature_c` の 5 分の平均が **30 ℃ を超えたら、メールを送る**例を説明します。

> メールアドレスは、`you@example.com` と書いてあります。自分のものに置き換えてください。

構成は、次のとおりです。

```
メトリクス (温度) ──> アラーム (30 ℃ を超えたら ALARM) ──> SNS トピック ──> メール
```

- **SNS** (Simple Notification Service): 通知を、メールなどに配る仕組みです。「トピック」(通知の宛先のまとまり) を作って、メールアドレスを「サブスクリプション」(購読) として登録します。
- アラームの状態は、`OK` (しきい値の範囲内)、`ALARM` (しきい値を超えた)、`データ不足` (値がまだ届いていない) の 3 つです。状態が変わったときに、設定したアクションが実行されます (この例では、`ALARM` になったときと、`OK` に戻ったときに、メールが届きます)。

#### 手順 1: SNS のトピックを作って、メールを登録する

**CLI の場合** (CloudShell など)

```bash
# トピックを作る (出力の TopicArn を控える)
aws sns create-topic --name thermo-alarm

# メールアドレスを登録する (TopicArn は、上の出力の値)
aws sns subscribe \
  --topic-arn arn:aws:sns:ap-northeast-1:${ACCOUNT_ID}:thermo-alarm \
  --protocol email \
  --notification-endpoint you@example.com
```

**コンソールの場合**: 「SNS → トピック → トピックの作成」で、タイプは「スタンダード」、名前は `thermo-alarm` にします。作ったトピックの「サブスクリプションの作成」で、プロトコルは「E メール」、エンドポイントは、通知を受け取るメールアドレスにします。

**どちらの場合も、確認が必要です。** 登録したメールアドレスに、件名「AWS Notification - Subscription Confirmation」のメールが届きます。**メールの中の「Confirm subscription」(購読の承認) を押してください**。押さないと、通知が届きません (迷惑メールのフォルダも確認してください)。承認すると、SNS の画面で、サブスクリプションの状態が「確認済み」になります。

#### 手順 2: アラームを作る

**CLI の場合**

```bash
aws cloudwatch put-metric-alarm \
  --alarm-name thermo-temperature-high \
  --alarm-description "The temperature of the SwitchBot is higher than 30 C" \
  --namespace Thermo \
  --metric-name "ED:2E:C4:46:3B:11_temperature_c" \
  --statistic Average --period 300 \
  --evaluation-periods 1 --datapoints-to-alarm 1 \
  --threshold 30 --comparison-operator GreaterThanThreshold \
  --treat-missing-data notBreaching \
  --alarm-actions arn:aws:sns:ap-northeast-1:${ACCOUNT_ID}:thermo-alarm \
  --ok-actions arn:aws:sns:ap-northeast-1:${ACCOUNT_ID}:thermo-alarm
```

| オプション | 意味 |
|:---|:---|
| `--statistic Average --period 300` | 5 分 (300 秒) の平均を、評価する |
| `--evaluation-periods 1 --datapoints-to-alarm 1` | 5 分の平均が 1 回でもしきい値を超えたら、ALARM にする。誤報を減らしたいときは、`--evaluation-periods 3 --datapoints-to-alarm 3` (15 分続いたら ALARM にする) のようにする |
| `--threshold 30 --comparison-operator GreaterThanThreshold` | 30 ℃ **より大きい**とき |
| `--treat-missing-data notBreaching` | データが来ないときは、しきい値を超えていないとみなす (ALARM にしない) |
| `--alarm-actions` | ALARM になったときに通知する、SNS トピック |
| `--ok-actions` | ALARM から OK に戻ったときに通知する、SNS トピック (省略すると、戻ったときの通知は来ない) |

**コンソールの場合**: 「CloudWatch → アラーム → アラームの作成」で、メトリクスは「Thermo」の `ED:2E:C4:46:3B:11_temperature_c` を選びます。統計は「平均値」、期間は「5 分」、条件は「静的」の「より大きい」、しきい値は `30` にします。「その他の設定」の「欠落データの処理」は、「欠落データを適正 (しきい値を超えていない) として処理」にします。アクションは、トリガーを「アラーム状態」にして、既存の SNS トピック `thermo-alarm` に通知します (`OK` に戻ったときも通知したいときは、「通知の追加」で、トリガーを「OK」にします)。アラーム名は `thermo-temperature-high` にします。作った直後の状態は「データ不足」ですが、数分たつと「OK」に変わります。

#### 手順 3: 通知を試す

温度を 30 ℃ まで上げなくても、アラームの状態を手で ALARM に変えて、メールが届くかどうかを試せます。

```bash
aws cloudwatch set-alarm-state \
  --alarm-name thermo-temperature-high \
  --state-value ALARM --state-reason "test"
```

- 数秒から数分で、件名「ALARM: "thermo-temperature-high" in Asia Pacific (Tokyo)」のメールが届きます。

メールには、次のことが書いてあります。

| 項目 | 内容 |
|:---|:---|
| `State Change` | `OK -> ALARM` (状態が、OK から ALARM に変わった) |
| `Reason for State Change` | `test` (`--state-reason` に書いた文字) |
| `Threshold` | 300 秒の期間のうち 1 回、30.0 より大きくなったとき (`GreaterThanThreshold 30.0`) |
| `Monitored Metric` | 名前空間 `Thermo`、メトリクス `ED:2E:C4:46:3B:11_temperature_c`、平均、300 秒、`TreatMissingData: notBreaching` |
| `State Change Actions` | `ALARM` になったとき、`thermo-alarm` に通知する (`--ok-actions` を付けたときは、`OK` に戻ったときも通知する) |

> **このメールを、人に見せたり、公開したりしないでください。** アカウント ID と、**購読を解除するリンク** (メールの末尾) が入っています。リンクを知っている人は、通知の購読を解除できます。

- 手で変えた状態は、次の評価 (最長 5 分後) で、実際の値に基づく状態に戻ります。戻ると、`OK` のメールが届きます (`--ok-actions` を付けたとき)。
- コンソールでは、アラームの画面の「アクション」→「テスト」(または、状態の変更) で、同じことができます。

アラームの状態は、「CloudWatch → アラーム」で見られます。ダッシュボードにアラームの状態を載せることもできます (ウィジェットの追加で、データ型「アラーム」を選びます)。

#### 変えるとき

| やりたいこと | 変える場所 |
|:---|:---|
| 寒いとき (例: 5 ℃ を下回ったら) | `--threshold 5 --comparison-operator LessThanThreshold` |
| ゲートウェイが止まったことを知りたい (データが来ない) | `--treat-missing-data breaching` にして、`--period 300 --evaluation-periods 3` のようにする (15 分間データが来ないと、ALARM になる) |
| 電池残量が少ないとき | メトリクス `ED:2E:C4:46:3B:11_battery`、`--threshold 20 --comparison-operator LessThanThreshold`、`--period 3600` |
| 通知を止める | アラームを削除する (`aws cloudwatch delete-alarms --alarm-names thermo-temperature-high`)、または、アクションを無効にする (`aws cloudwatch disable-alarm-actions --alarm-names thermo-temperature-high`) |
| 送り先を増やす | SNS のトピックに、メールアドレスをもう 1 つ登録する (手順 1)。登録した人は、それぞれ承認が必要 |

料金の目安は、アラーム 1 つあたり月に数十円以下です。メールの通知は、月 1,000 件まで無料です (料金は、リージョンと時期で変わります。最新の料金表で確認してください)。

### 7.2 Thermo ノードの温度と湿度

ノードの値は、`temperature_c` と `humidity` (DHT11 のとき) です ([AWS_SETUP.md](AWS_SETUP.md) の 1.1)。トピックは、`thermo/CLIENT_ID/NODE_ADDRESS/temperature` で、**ノードのアドレスは 3 番目**です (SwitchBot は 4 番目)。そのため、メトリクス名には `${topic(3)}` を使います。SwitchBot のルール (4 章) とは別に、ノード用のルールを作ります。

```bash
cat > rule-node.json <<JSON
{
  "sql": "SELECT temperature_c, humidity FROM 'thermo/+/+/temperature'",
  "awsIotSqlVersion": "2016-03-23",
  "ruleDisabled": false,
  "actions": [
    {
      "cloudwatchMetric": {
        "roleArn": "arn:aws:iam::${ACCOUNT_ID}:role/thermo-iot-cloudwatch",
        "metricNamespace": "Thermo",
        "metricName": "\${topic(3)}_temperature_c",
        "metricValue": "\${temperature_c}",
        "metricUnit": "None"
      }
    },
    {
      "cloudwatchMetric": {
        "roleArn": "arn:aws:iam::${ACCOUNT_ID}:role/thermo-iot-cloudwatch",
        "metricNamespace": "Thermo",
        "metricName": "\${topic(3)}_humidity",
        "metricValue": "\${humidity}",
        "metricUnit": "Percent"
      }
    }
  ]
}
JSON

aws iot create-topic-rule --rule-name thermo_node_to_cloudwatch \
  --topic-rule-payload file://rule-node.json
```

- IAM ロールは、3 章のものを、そのまま使えます (名前空間 `Thermo` への書き込みの権限です)。
- SwitchBot のルールの SQL (`'thermo/+/switchbot/+'`) と、このルールの SQL (`'thermo/+/+/temperature'`) は、同じトピックには合わないので、メッセージが二重に処理されることはありません。
- 湿度を測れないセンサ (ADC など) のときは、`humidity` が出力されないので、湿度のアクションだけが実行されず、湿度のメトリクスはできません。
- ルールを作ったら、5 章と同じ手順で、`aws cloudwatch list-metrics --namespace Thermo` を実行して、`00:AA:01:00:00:42_temperature_c` のような名前が表示されることを確認します。
- グラフにするには、6.2 の `metrics` に、`[ "Thermo", "00:AA:01:00:00:42_temperature_c" ]` の行を足します (自分のノードのアドレスに置き換えます)。コンソールで作るときは、6.1 の手順 3 で、一覧からノードのメトリクスを選びます。
- ノードのアドレスは、MQTT テストクライアントで `thermo/#` を購読すると、トピックから分かります。

### 7.3 細かいデータを残す・分析する

| 方法 | 向いていること |
|:---|:---|
| IoT ルール → S3 (JSON) → Athena | 10 秒ごとの、すべてのデータを、安く残して、SQL で分析する |
| IoT ルール → DynamoDB ([AWS_DYNAMODB.md](AWS_DYNAMODB.md)) | 最新の値をすぐ読み出す。ほかのアプリから使う |
| Amazon Managed Grafana (+ CloudWatch) | 見た目や、複数のデータの組み合わせを、自由に作る |

### 7.4 ダッシュボードを、ほかの人に見せる (共有)

作ったダッシュボードは、**AWS にログインできる人だけ**が見られます。ほかの人にも見せたいときは、コンソールで、ダッシュボードを「共有」します (共有の設定は、コンソールだけでできます)。

「CloudWatch → ダッシュボード」で、ダッシュボード (`thermo`) を開いて、右上の「アクション」→「ダッシュボードの共有」を押します。

| 方法 | 見られる人 | 注意 |
|:---|:---|:---|
| ダッシュボードを共有し、ユーザー名とパスワードを要求する | 登録したメールアドレスの人だけ (最大 5 人) | 家族などに見せるには、こちらが安全 |
| ダッシュボードを**パブリックに共有する** | **リンクを知っている人は、誰でも** | 下の警告を、よく読む |
| シングルサインオン (SSO) で、すべての CloudWatch ダッシュボードを共有する | SSO プロバイダーの、すべてのユーザー | SSO の設定が、別に必要 |

> **注意**: どの方法でも、共有された人のブラウザは、アカウントの**読み取り専用の一時的な権限**を持ちます (書き込みや削除はできません)。ただし、共有するダッシュボードに載っていないものも、読み取れます (アカウント内の全てのメトリクスの名前、全ての EC2 インスタンスの名前とタグなど)。共有のために、Cognito のリソース (ユーザープール、ID プール) と IAM ロールが、アカウントに作られます (バージニア北部 `us-east-1`)。リンクとログイン画面の URL には、アカウント ID が入ります。

**家族に室温を見せたいだけなら、パブリックの共有はおすすめしません。** アカウントのメトリクスの名前が、リンクを知っている誰にでも見えてしまうためです。

- **ユーザー名とパスワードで共有する**: 「共有を開始」を押して、見せたい人のメールアドレスを入れて、ポリシーを確認して、受け入れます。共有のリンクが作られます。共有される人は、リンクを開いて、ログイン画面に、メールアドレスと、招待メールに書かれた一時パスワードを入力します (AWS アカウントのログインではなく、共有のために作られた、閲覧用のユーザーです)。共有する人の追加と削除は、同じ画面の「編集」で行います。
- **パブリックに共有する**: 「共有を開始」を押して、確認の入力欄に「共有」と入力し、ポリシーを受け入れます。共有できるリンクが作られます。
- **共有をやめる**: 「ダッシュボードの共有」の画面で、それぞれの方式の「共有を停止」を押します。共有のために作られた IAM ロール (`CWDBSharing-...`) と Cognito のリソースが、残ることがあります。不要なら、IAM と Cognito (バージニア北部) の画面で削除します。

## 8. うまくいかないとき

| 症状 | 確認すること |
|:---|:---|
| メトリクスができない | MQTT テストクライアントで、`thermo/#` に、メッセージが届いているか。ルールの SQL のトピック (`thermo/+/switchbot/+`) が、合っているか |
| `list-metrics` の結果が空 | 数分待つ。リージョンが、ルールを作ったリージョンと同じか確認する (`--region` を付ける) |
| ルールが動かない | 「IoT Core → ルール」で、ルールが「有効」になっているか。「IoT Core → 設定 → ログ」でログを有効にして、CloudWatch Logs の `AWSIotLogsV2` で、エラーを確認する |
| 権限のエラー (`AccessDenied`) | ロールの信頼ポリシーが、`iot.amazonaws.com` か。権限ポリシーの名前空間が、`Thermo` か |
| アラームのメールが、届かない | SNS のサブスクリプションが「確認済み」か (確認メールの承認)。迷惑メールのフォルダ。アラームの「アクション」に、`thermo-alarm` が入っているか。リージョンが、東京か |
| アラームの状態が、ずっと「データ不足」 | メトリクス名が、正しいか (空白、MAC アドレス)。ゲートウェイが、AWS に送信しているか。ルールが、動いているか (5 章) |
| メトリクス名の先頭に、空白がある (`" ED:..."`) | ルールのアクションの、メトリクス名の先頭の空白を消す (4.4)。空白付きの古い名前のメトリクスは残るが、新しい値は入らなくなる |
| 電池残量のメトリクスだけできない | メッセージに `battery` が入っているか確認する (電池残量が不明なときは、入らない) |
| グラフの値が 1 分ごとにしか変わらない | CloudWatch の最小の期間は 1 分なので、正常 |

## 9. 料金の目安

料金は、リージョンと時期で変わります。最新の料金表で、確認してください。

- **IoT Core のルール**: メッセージ 1 通で、ルールが 1 回実行されて、アクションが 3 回実行されます。1 台が 10 秒に 1 通送ると、1 日にメッセージが 8,640 通 (アクションは約 26,000 回)、1 か月にメッセージが約 26 万通 (アクションは約 78 万回) です。
- **CloudWatch**: 料金の対象は、カスタムメトリクス (機器 1 台あたり 3 つ) とダッシュボードです。無料枠は、カスタムメトリクスが 10 個まで、ダッシュボードが 3 つまでです (無料枠の条件は、変わることがあります)。

使わなくなったときは、課金が続かないように、ルール (`aws iot delete-topic-rule --rule-name thermo_switchbot_to_cloudwatch`)、ダッシュボード、アラームを削除してください。
