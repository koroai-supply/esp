# 外部コンポーネント

SDK本体のMIT Licenseとは別に、依存物のライセンス条件も適用されます。依存ライブラリはビルド時に公式の配布元から取得します。配布するファームウェアにも、該当する著作権表示とライセンス条件を適用してください。

| コンポーネント | 用途 | ライセンス・出典 |
| --- | --- | --- |
| certifi / Mozilla CA bundle | ESPのルート証明書。同梱 | [MPL-2.0と出典](esp/certs/README.md)、[ライセンス本文](esp/certs/LICENSE) |
| ArduinoJson 7.4.2 | ESP/ArduinoのJSON | [MIT](https://github.com/bblanchon/ArduinoJson/blob/v7.4.2/LICENSE.txt) |
| ArduinoHttpClient 0.6.1 | ArduinoのHTTP | [Apache-2.0](https://github.com/arduino-libraries/ArduinoHttpClient/blob/0.6.1/LICENSE) |
| Crypto 0.4.0 | ArduinoのSHA-256 | [MITほか各ソースの表示](https://github.com/OperatorFoundation/Crypto) |
| Arduino-ESP32 / ESP-IDF / mbedTLS | ESPのボードコア・TLS・NVS | [Espressifのライセンス](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md)、個別依存の表示 |
| ArduinoCore-renesas / WiFiS3 / EEPROM | UNO R4 WiFiコア | [Arduino公式ソースのライセンス](https://github.com/arduino/ArduinoCore-renesas)、各ファイルの表示 |
| Python | Linux実行環境 | [PSF License](https://docs.python.org/3/license.html) |

この一覧は依存ライブラリそのものの再ライセンスを行うものではありません。
