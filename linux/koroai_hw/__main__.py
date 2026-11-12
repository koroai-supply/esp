import os
from pathlib import Path
import time
from .client import Client, HttpsTransport, valid_jan


def main():
    jan = os.environ.get('JAN', '')
    if jan and not valid_jan(jan):
        raise SystemExit('JANに有効なJAN-8/13を設定するか、空にしてクイック注文を使ってください。')
    state = os.environ.get('KOROAI_STATE_DIR', str(Path.home() / '.local/state/koroai-dash'))
    with Client(os.environ.get('API_KEY', ''), state) as client:
        print('sandbox: テスト注文' if os.environ['API_KEY'].startswith('kr_test_') else 'live: 実注文')
        if not jan:
            try:
                product = HttpsTransport(os.environ['API_KEY']).quick_order_product()
            except OSError:
                print('クイック注文の商品を確認できませんでした。押下時に注文を試みます。')
            else:
                print(f"クイック注文の商品: {product['name']}（JAN {product['jan']}）" if product
                      else 'ホームのクイック注文の商品が未設定です。アプリで設定してください。')
        while True:
            if client.blocked:
                raise SystemExit('確認が必要な応答を保存しました。注文結果と設定を確認してください。')
            if client.pending is None:
                if jan:
                    input('Enterで1パッケージ注文（終了はCtrl-C）: ')
                    client.start(jan)
                else:
                    input('Enterでホームのクイック注文の商品を1パッケージ注文（終了はCtrl-C）: ')
                    client.start_quick()
            reply = client.attempt()
            if reply is not None:
                print(f'注文API: HTTP {reply.http_status} / {reply.outcome}')
                if reply.outcome == 'accepted':
                    client.acknowledge()
                    print(f'受付済み（JAN {reply.jan}）。決済・配送の完了ではありません。')
                    time.sleep(60)
                elif reply.outcome == 'not_set':
                    print('ホームのクイック注文の商品が未設定です。注文していません。アプリで設定してから押し直してください。')
            if client.pending is not None and not client.blocked:
                time.sleep(min(60, max(1, client.retry_at - int(time.time()))))


if __name__ == '__main__':
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        pass
