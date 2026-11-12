# Arduino SDK — UNO R4 WiFi

Arduino UNO R4 WiFi用のダッシュボタンSDKです。外付けボタンをD2とGNDの間に接続し、1回押すとホームのクイック注文の商品（`JAN`を設定した場合はその商品）を1パッケージ注文します。ESP32用のSDKは[../esp/](../esp/README.md)です。

Arduino CLI 1.6.0またはArduino IDEと、Renesas UNO R4 Boards 1.6.0を使います。

```sh
arduino-cli core update-index
arduino-cli core install arduino:renesas_uno@1.6.0
arduino-cli lib install ArduinoJson@7.4.2 ArduinoHttpClient@0.6.1 Crypto@0.4.0
python3 arduino/configure.py
```

リポジトリのルートで実行してください。生成した`arduino/Koroai/examples/DashButton/dash_config.h`の`SSID`、`PW`、`API_KEY`を編集します。`JAN`は空のままならクイック注文、JAN-8/13を書けばその商品の注文です。自動生成の`INSTALLATION_NONCE`は編集しません。設定ファイルはGitに含まれません。

```sh
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi \
  --library arduino/Koroai arduino/Koroai/examples/DashButton
arduino-cli upload -p /dev/ttyACM0 --fqbn arduino:renesas_uno:unor4wifi \
  arduino/Koroai/examples/DashButton
```

ポートは環境に合わせて変更します。Arduino IDEでは`arduino/Koroai`をライブラリとして追加し、DashButtonサンプルを開きます。シリアルは115200bps。`kr_test_`でテスト注文、`kr_live_`で実注文です。公開APIは提供開始前で、実機・実APIでの確認は未実施です。

クイック注文の商品はアプリの連携のタブやMCPで設定し、SDKからは変えられません。未設定（`quick_order_not_set`）のときは注文されないため、その押下を取り消して待機に戻り、シリアルに未設定と表示します。60秒後から押し直せます。SDKでは`order.startQuick()`がクイック注文、`order.start(jan)`が商品指定の注文です。

初期化時のnonceとボードのMACアドレスからinstallation IDを作り、保存後はそのIDと注文連番を引き継ぎます。新しい押下だけが連番を進め、同じ注文の再送・再起動は同じキー・JAN・数量を使います。長押しとチャタリングを抑止し、受付後60秒は新しい注文を受け付けません。

EEPROMの0〜1023と4096〜5119バイトをSDK専用に予約します。別の用途で書き込まないでください。2つのスロットを交互に使い、世代番号とCRCを確認してから、最後のコミット印を確定します。書き込み中断は直前の有効な記録から復元し、コミット済みデータが壊れていれば停止します。書き込みはバイトごとの消去を避け、ブロック単位でまとめます。外部送信は保存成功後だけです。EEPROMの実際の電源断と耐久性は実機での検証が必要です。

HTTPSはWiFiS3の`WiFiSSLClient`とボードのWi-FiブリッジFWに組み込まれたCAバンドルを使います。Wi-Fi FWを更新し、[Arduino公式SSLサンプル](https://github.com/arduino/ArduinoCore-renesas/blob/main/libraries/WiFiS3/examples/WiFiWebClientSSL/WiFiWebClientSSL.ino)と同じ証明書検証経路を使ってください。リダイレクトを追跡しません。

エラー・認証変更時は未確定の注文を保持して停止します。停止からの自動消去はありません。Webなどで注文結果を確認してから個別に復旧してください。EEPROMを初期化する必要がある場合は、旧注文結果を確認・記録した上で、設定ファイルを安全に退避し、`configure.py`で新しいnonceを生成したFWを書き込みます。同じnonceのFWを使って連番だけ初期化するとキーが再利用されるため禁止です。保存記録の巻き戻し・複製も行わないでください。

本SDKはloopタスクで直列に使います。HTTPとEEPROM書き込みは同期式です。対応ボードはUNO R4 WiFiに限定し、AVR UNOなどのWi-Fi非搭載機は対象外です。

## ライセンス

SDKとサンプルは **[MIT License](../LICENSE)** です。依存ライブラリ・ボードコアの条件は[THIRD_PARTY.md](../THIRD_PARTY.md)を参照してください。ESP向けCA証明書はこのArduinoライブラリには同梱しません。

通信失敗時の再送間隔は60秒から段階的に延ばし、最大1時間にします。サーバーの`Retry-After`が長い場合はそちらを優先します。待機期限と間隔は保存し、長期障害中の通信とフラッシュ書き込みを抑えます。
