"""Create a private sketch configuration and a one-time installation nonce."""
from pathlib import Path
import secrets

path = Path(__file__).parent / 'Koroai/examples/DashButton/dash_config.h'
with path.open('x') as target:
    target.write('''#pragma once
#define SSID "YOUR_WIFI_SSID"
#define PW "YOUR_WIFI_PASSWORD"
#define API_KEY "kr_test_REPLACE_WITH_YOUR_KEY"
// 空ならホームのクイック注文の商品。特定の商品はJAN-8/13を書く
#define JAN ""
''')
    target.write(f'#define INSTALLATION_NONCE "{secrets.token_hex(16)}"\n')
print(f'{path}: SSID, PW, API_KEYを編集してください。JANは空ならクイック注文です。')
