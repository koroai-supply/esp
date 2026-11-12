#include <Arduino.h>
#include <WiFi.h>
#include <ctime>
#include <Koroai.h>
#include <Button.h>
#if __has_include("dash_config.h")
#include "dash_config.h"
#else
#include "dash_config.example.h"
#endif

#ifndef JAN
#define JAN ""
#endif
#ifndef DASH_BUTTON_PIN
#define DASH_BUTTON_PIN 0
#endif
extern const char roots[] asm("_binary_certs_roots_pem_start");
koroai::NvsStore store;
koroai::HttpsTransport transport(API_KEY, roots);
koroai::Order order(store, transport);
bool ready = false;
koroai::Button button;
uint64_t nextPressAt = 0;
// Empty JAN: order the home's quick-order product (set in the app or MCP).
const bool quick = JAN[0] == '\0';

void setup() {
  Serial.begin(115200);
  pinMode(DASH_BUTTON_PIN, INPUT_PULLUP);
  if (!koroai::validKey(API_KEY) || (!quick && !koroai::validJan(JAN))) {
    Serial.println("dash_config.h の API_KEY と JAN を確認してください。JANは空にするとクイック注文です。");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SSID, PW);
  if (!order.begin(koroai::credentialScope(API_KEY), koroai::newInstallationId())) {
    Serial.println("保存注文の読込・認証照合に失敗。注文を停止しました。");
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.println(strncmp(API_KEY, "kr_test_", 8) == 0 ? "sandbox: テスト注文" : "live: 実注文");
  Serial.println(quick ? "BOOTボタンでホームのクイック注文の商品を1パッケージ注文。未確定注文は同じキーで再送します。"
                       : "BOOTボタンで1パッケージ注文。未確定注文は同じキーで再送します。");
  ready = true;
}
void loop() {
  delay(10);
  if (!ready) return;
  uint64_t now = static_cast<uint64_t>(time(nullptr));
  if (now < 1735689600) return;
  bool raw = digitalRead(DASH_BUTTON_PIN) == LOW;
  if (button.pressed(raw, millis()) && now >= nextPressAt && order.state() == koroai::State::Idle) {
    if (!(quick ? order.startQuick() : order.start(JAN))) {
      Serial.println("注文の保存に失敗。送信せず停止しました。");
    }
  }
  if (WiFi.status() != WL_CONNECTED || !order.attempt(now)) return;
  Serial.printf("注文API: HTTP %d\n", order.reply().httpStatus);
  if (order.reply().outcome == koroai::Outcome::NotSet && order.state() == koroai::State::Idle) {
    Serial.println("ホームのクイック注文の商品が未設定です。注文していません。アプリで設定してから押し直してください。");
    nextPressAt = static_cast<uint64_t>(time(nullptr)) + 60;
  } else if (order.state() == koroai::State::Accepted) {
    Serial.printf("注文を受け付けました（JAN %s）。決済・配送の完了ではありません。\n", order.reply().jan.c_str());
    if (!order.acknowledge()) Serial.println("保存注文の解除に失敗。停止しました。");
    nextPressAt = static_cast<uint64_t>(time(nullptr)) + 60;
  } else if (order.state() == koroai::State::Blocked) {
    Serial.println("設定・権限・購入上限などの確認が必要です。自動注文を停止しました。");
  } else if (order.state() == koroai::State::StorageError) {
    Serial.println("注文記録の保存に失敗。停止しました。");
    ready = false;
  } else {
    Serial.println("結果未確定。同じ注文キーで待機後に再送します。");
  }
}
