import os
from pathlib import Path
import time
from .client import Client, valid_jan


def main():
    jan = os.environ.get('JAN', '')
    if not valid_jan(jan):
        raise SystemExit('JANに有効なJAN-8/13を設定してください。')
    state = os.environ.get('KOROAI_STATE_DIR', str(Path.home() / '.local/state/koroai-dash'))
    with Client(os.environ.get('API_KEY', ''), state) as client:
        print('sandbox: テスト注文' if os.environ['API_KEY'].startswith('kr_test_') else 'live: 実注文')
        while True:
            if client.blocked:
                raise SystemExit('確認が必要な応答を保存しました。注文結果と設定を確認してください。')
            if client.pending is None:
                input('Enterで1パッケージ注文（終了はCtrl-C）: ')
                client.start(jan)
            reply = client.attempt()
            if reply is not None:
                print(f'注文API: HTTP {reply.http_status} / {reply.outcome}')
                if reply.outcome == 'accepted':
                    client.acknowledge()
                    print('受付済み。決済・配送の完了ではありません。')
                    time.sleep(60)
            if client.pending is not None and not client.blocked:
                time.sleep(min(60, max(1, client.retry_at - int(time.time()))))


if __name__ == '__main__':
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        pass
