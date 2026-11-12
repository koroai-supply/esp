# Linux SDK

Python 3.10以降とOSのCA証明書を使う、追加の実行時依存がないSDKです。Wi-FiはOS側で接続してください。API草案への実接続は未確認です。

```sh
python3 -m venv .venv
.venv/bin/pip install -e .
export API_KEY='自分のkr_test_キー'
export JAN='注文対象のJAN'
.venv/bin/koroai-dash
```

Enterがダッシュボタンです。1回で1パッケージを注文し、受付後は60秒待ちます。実機のGPIOを使う場合は、押下を検出するプログラムから`Client.start()`を呼び、保持中の連打を抑止してください。

```python
from koroai_hw import Client

with Client(api_key, '/var/lib/koroai-dash') as client:
    if client.pending is None and not client.blocked:
        client.start(jan)  # 新しい押下のときだけ呼ぶ
    reply = client.attempt()  # 同じ注文の再送。待機中はNone
    if reply and reply.outcome == 'accepted':
        client.acknowledge()
```

`Client`を開いている間はプロセスロックを保持します。同じ状態ディレクトリで同時起動すると後のプロセスは失敗します。スレッド間でも直列に呼び出してください。
標準の状態ディレクトリは`~/.local/state/koroai-dash`、変更は`KOROAI_STATE_DIR`で行います。別ディレクトリを指定すると別の端末として扱うため、未確定注文がある状態で切り替えないでください。ローカルの永続ファイルシステムを使い、NFSや揮発ディスクを避けてください。

installation IDと注文連番からキーを作り、内容・連番・再送期限を一緒に一時ファイルへ書き、fsync・rename・ディレクトリのfsyncで確定してから送信します。ファイルは600で作成します。結果未確定時は同じキー・JAN・数量で再送し、新規注文を拒否します。保存失敗後のインスタンスは再利用せず、開き直して永続記録から復元します。

通信先は固定のHTTPS、リダイレクトと環境変数によるプロキシは無効です。201の注文ID・モード・JAN・数量・状態を確認して受付と判定します。壊れた応答と5xxは再送、429はRetry-After秒以上待機、4xx（408/429以外）は停止を保存します。APIキー変更・状態ファイル破損・結果未確認の状態削除では自動復旧しません。

状態ファイル・ディレクトリを他ユーザーに編集させないでください。バックアップからの巻き戻しや複製は連番を再利用するため禁止です。停止から復旧する際はWebなどで注文結果を確認し、元の記録を保全して個別に対応してください。状態削除は注文取消ではありません。

```sh
PYTHONPATH=. python3 -m unittest discover -s tests -v
```

## ライセンス

SDKとサンプルは **[MIT License](../LICENSE)** です。PythonおよびOS側のCA証明書は、それぞれの配布元のライセンスに従います。[外部コンポーネント](../THIRD_PARTY.md)も参照してください。

通信失敗時の再送間隔は60秒から段階的に延ばし、最大1時間にします。サーバーの`Retry-After`が長い場合はそちらを優先します。待機期限と間隔は保存し、長期障害中の通信とフラッシュ書き込みを抑えます。
