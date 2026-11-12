# 頃合 ESP32 SDK

ESP32を頃合のダッシュボタンにするArduino SDKと、標準プロジェクトです。
`SSID`、`PW`、`API_KEY`を設定すると、BOOTボタンでホームのクイック注文の商品を1パッケージ注文します。`JAN`を設定すればその商品を注文します。

**公開APIは開発中です。** このSDKは[公開API草案 v0.2.0](https://github.com/koroai-supply/api/blob/main/openapi.yaml)に合わせた実装で、実API・実機の動作確認はまだです。
`kr_test_`はテスト注文、`kr_live_`は実注文です。実注文はホームの月間購入予算に従います。受付は決済・配送の完了を意味しません。

## はじめる

PlatformIO Core 6.1.19以降とUSB接続できるESP32開発ボードを用意します。標準はESP32 Dev Module、ESP32-S3 DevKitCにも対応します。BOOTはGPIO 0の押下時LOWです。

```sh
cp include/dash_config.example.h include/dash_config.h
```

`include/dash_config.h`を編集します。

```cpp
#pragma once
#define SSID "Wi-FiのSSID"
#define PW "Wi-Fiのパスワード"
#define API_KEY "kr_test_から始まる自分のテスト用キー"
#define JAN ""  // 空ならクイック注文。特定の商品はJANコードを書く
```

`JAN`が空なら、ホームのクイック注文の商品を注文します（`POST /v1/quick-order`、本文は`{}`、数量は常に1）。商品はアプリの連携のタブやMCPで設定し、SDKからは変えられません。未設定のホームでは`quick_order_not_set`（422）が返り、注文はされません。SDKはその押下を取り消して待機に戻し、シリアルに未設定と表示します。60秒後から押し直せます。
受付時は、サーバーが選んだ商品のJANをシリアルに表示します。

JANを設定する場合は文字列のまま8桁または13桁で指定します。先頭ゼロを保持し、チェックディジットも検証します。数量は標準で1パッケージです。APIキーは所定のprefixと43文字のbase64url部分を持つ形式に限ります。
設定ファイルはGit管理から除外しています。キーはシリアル出力しません。設定値はファームウェアに含まれるため、ビルド成果物を公開しないでください。

```sh
pio run -t upload
pio device monitor
# ESP32-S3の場合
pio run -e esp32s3 -t upload
```

ビルドはこの`esp/`ディレクトリで行います。リポジトリのルートからは`pio run -d esp`を使えます。C++の共通ロジックは`../arduino/Koroai/src/`にあるため、リポジトリ全体を取得してください。

通常の書き込みではNVSを保持します。起動後、Wi-Fi接続と時刻同期を待ち、一度BOOTを離してから押してください。起動中の長押しはESP32自体が書き込みモードに入ることがあります。
別のボタンは`DASH_BUTTON_PIN`をビルドフラグで指定し、GPIOとGNDの間に接続します。

50msのチャタリング除去を行い、長押しは1回として扱います。未確定の注文がある間の押下は捨て、受付後60秒は新しい注文を受け付けません。待機中の押下を後でまとめて送る動作はありません。

## 注文の冪等性

注文の単位は「待機中に受け付けた、新しい押下」です。同じJANのすべての注文を永久に1件にする仕組みではありません。

1. 初期化時に端末のinstallation IDを作り、NVSに保存します。
2. 新しい押下を受け付けたときだけ注文連番を1増やし、`dash-<installation ID>-<注文連番>`を作ります。
3. 連番・JAN・数量・キー・APIキーのSHA-256を**単一のNVS journalとして保存してから**通信します。保存失敗時は送信を止めます。
4. タイムアウト、5xx、応答喪失、壊れた成功応答ではjournalを保持し、同じキー・JAN・数量で再送します。再起動後も同じ注文を復元します。JANの設定変更も未確定注文には反映しません。
5. 正常な201応答を確認してから未確定注文を解除します。解除直前に電源が落ちた場合も同じキーを再送します。次の新しい押下だけが次の連番になります。

installation IDだけが初期化時の乱数です。リトライごとに乱数・時刻から新しいキーを作りません。通常の再起動・ファーム更新ではIDと連番を引き継ぎます。

429は`Retry-After`秒以上待ちます。他の再送も最短60秒間隔です。待機期限はUTC秒でjournalに保存するため、通常の再起動でも保持します。不正な`Retry-After`は自動再送を長期間停止する扱いにします。API草案は秒数形式を規定しています。
401/403/404/409/422などの4xx（408/429を除く）とリダイレクトは自動で解決せず、停止を保存します。

APIキー変更中に未確定注文がある場合も停止します。別ホーム・sandbox/liveへ注文を移して再送しません。サーバーはキーを更新しても同じホーム・モードの照合を引き継ぎますが、この初期SDKはホーム識別を安全に検証する方法がないため自動移行しません。

停止からの復旧は、Webなどで注文結果を確認してから個別に対応してください。この版には未確定注文を強制破棄するボタンはありません。NVSの初期化・過去バックアップの復元・journalを他端末へ複製する操作は注文結果を確認せずに行わないでください。NVSを消すと旧注文の再送情報も失われ、サーバーの注文を取り消せません。

二重注文の防止は、同じキー・同じ内容を照合するサーバー側の契約との組み合わせで成立します。API草案が変わった場合はSDK側も追従が必要です。

## SDKを組み込む

`lib/Koroai`がArduinoライブラリです。JSONとHTTP/NVSから独立した`Order`に、`Store`と`Transport`を渡します。デフォルトの実装は`NvsStore`と`HttpsTransport`です。

- `begin(scope, newInstallationId)`：journal復元。IDの候補はjournalがない最初だけ使います。Wi-Fiを有効にした後に`newInstallationId()`を呼びます。
- `start(jan, qty = 1)`：新しい論理注文を保存。待機中だけ成功し、数量は1〜10。
- `startQuick()`：ホームのクイック注文の商品を1パッケージ注文する論理注文を保存。冪等キー・再送・押下の扱いは`start`と同じです。`reply().outcome`が`Outcome::NotSet`なら商品が未設定で、注文は作られず待機に戻ります。受付時の`reply().jan`はサーバーが選んだ商品です。
- `attempt(utcSeconds)`：待機期限を過ぎていれば1回送信。時刻同期後のUTC秒を渡します。
- `reply()`：HTTPステータス・結果・注文ID。エラー本文や資格情報はログに出しません。
- `acknowledge()`：正常受付を確認した注文を解除。保存失敗時は停止します。

複数タスクから同時に呼ばず、Arduinoのloopタスクで直列に使います。HTTP/TLS処理は同期式です。省電力・電池駆動・OTA・Wi-Fi設定画面・注文照会/取消APIはこの版の範囲外です。

接続先は`https://api.koroai.supply/v1/orders`（クイック注文は`/v1/quick-order`）に固定しています。CA・ホスト名を検証し、リダイレクトに資格情報を渡しません。Mozillaのルート証明書を同梱するため、利用者によるCA設定は不要です。証明書更新時は`certs/README.md`を参照してください。

## 検証

```sh
mkdir -p build
c++ -std=c++11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
  -Ilib/Koroai/src lib/Koroai/src/Order.cpp test/native/order_test.cpp -o build/order_test
./build/order_test
pio run -e esp32dev -e esp32s3
```

CIも同じネイティブテストと両ボードのビルドを実行します。ネイティブテストはNVSと通信を模擬し、論理キーの連番、応答喪失、受付直後/解除後の再起動、保存失敗、認証変更、429待機、停止状態、長押し・チャタリング・時計カウンタの周回を確認します。実機の電源断、TLS、Wi-Fi再接続、公開APIへの注文は別途受入確認が必要です。

## ライセンス

SDKとサンプルは **[MIT License](../LICENSE)**、同梱のCA証明書は **[MPL-2.0](certs/LICENSE)** です。[外部コンポーネント](../THIRD_PARTY.md)も参照してください。

通信失敗時の再送間隔は60秒から段階的に延ばし、最大1時間にします。サーバーの`Retry-After`が長い場合はそちらを優先します。待機期限と間隔は保存し、長期障害中の通信とフラッシュ書き込みを抑えます。
