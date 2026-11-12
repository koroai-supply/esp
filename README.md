# 頃合 Hardware SDKs

頃合の公開APIで注文する、オープンソースのハードウェア向けSDKです。
ESP32・Linux・Arduinoの3種類を用意しています。旧リポジトリ名は`esp`、現在は`hw`です。

| ディレクトリ | 対応環境 | デフォルトのプロジェクト |
| --- | --- | --- |
| [esp/](esp/README.md) | ESP32 / ESP32-S3、Arduinoフレームワーク | BOOTボタンで1パッケージ注文 |
| [linux/](linux/README.md) | Linux、Python 3.10以降 | Enterで1パッケージ注文。GPIOへの組み込みも可能 |
| [arduino/](arduino/README.md) | Arduino UNO R4 WiFi | D2–GNDに接続したボタンで1パッケージ注文 |

ESP版は標準プロジェクトの`SSID`、`PW`、`API_KEY`、`JAN`を設定してビルドします。
Arduino版も同じ4項目を設定します。Linux版はOS側でネットワークへ接続し、`API_KEY`と`JAN`を環境変数に指定します。

**公開APIは開発中で、まだ提供開始していません。** 各SDKは[API草案 v0.2.0](https://github.com/koroai-supply/api/blob/main/openapi.yaml)に合わせた初期実装です。実APIでの注文と実機の動作確認は未実施です。`kr_test_`はテスト注文、`kr_live_`は実注文になります。

## 冪等性

新しい注文イベントだけが連番を進めます。`dash-<端末のinstallation ID>-<注文連番>`をキーにし、JAN・数量・キー・連番を永続保存してから送信します。再送や再起動ではキーを作り直しません。未確定の注文がある間は新しい注文を拒否し、APIキー変更時は保存中の注文を別ホームやモードへ移しません。

ESPとArduinoは同じC++の注文状態ロジックを使います。Linuxも同じ注文単位とキー形式で、プロセスロックとファイルの原子的保存を行います。サーバー側の冪等性契約と合わせて二重注文を防ぎます。保存情報の削除・巻き戻し・端末間の複製は、注文結果を確認せずに行わないでください。

## 開発

GitHub ActionsはC++の注文状態テスト、JSON検証、EEPROM書き込み中断600パターン、Linuxの通信・永続化テスト、ESP32/ESP32-S3とUNO R4 WiFiのコンパイルを実行します。最終判定は`required-ci`です。実機・実APIの受入確認は別に行います。

C++共通ロジックの正本は`arduino/Koroai/src/`です。ESP版はそこを参照するため、ESP版をビルドするときもこのリポジトリ全体を取得してください。

## ライセンス

このリポジトリのSDK・サンプルは **[MIT License](LICENSE)** です。
ESP版に同梱するMozillaルート証明書は **[MPL-2.0](esp/certs/LICENSE)** で、[出典・更新手順](esp/certs/README.md)を記載しています。
ArduinoJson、ArduinoHttpClient、Crypto、各ボードのコアなどの外部依存は、それぞれのライセンスに従います。[依存ライブラリのライセンス](THIRD_PARTY.md)も参照してください。

通信失敗時の再送間隔は60秒から段階的に延ばし、最大1時間にします。サーバーの`Retry-After`が長い場合はそちらを優先します。待機期限と間隔は保存し、長期障害中の通信とフラッシュ書き込みを抑えます。
