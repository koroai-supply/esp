#include <Koroai.h>
#include "dash_config.h"

koroai::EepromBytes bytes;
koroai::DurableStore store(bytes);
koroai::HttpsTransport transport(API_KEY);
koroai::Order order(store, transport);
koroai::Button button;
bool ready = false;
uint64_t nextPressAt = 0;
const int buttonPin = 2;  // External button between D2 and GND.

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  if (!koroai::validKey(API_KEY) || !koroai::validJan(JAN)) {
    Serial.println("API_KEYとJANを確認してください。");
    return;
  }
  WiFi.begin(SSID, PW);
  // Provisioned nonce + board MAC: each board has its own logical installation.
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char device[80];
  snprintf(device, sizeof(device), "%s-%02x%02x%02x%02x%02x%02x", INSTALLATION_NONCE,
           mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  std::string id = koroai::credentialScope(device).substr(0,32);
  ready = order.begin(koroai::credentialScope(API_KEY), id);
  Serial.println(ready ? "D2ボタンで1パッケージ注文。" : "保存注文の確認が必要です。停止しました。");
  Serial.println(strncmp(API_KEY,"kr_test_",8)==0 ? "sandbox: テスト注文" : "live: 実注文");
}
void loop() {
  delay(10);
  if (!ready) return;
  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastConnect = 0;
    if (millis()-lastConnect >= 30000) { lastConnect=millis(); WiFi.begin(SSID,PW); }
    return;
  }
  uint64_t now = WiFi.getTime();
  if (now < 1735689600) return;
  if (button.pressed(digitalRead(buttonPin)==LOW,millis()) && now>=nextPressAt && order.state()==koroai::State::Idle) {
    if (!order.start(JAN)) { Serial.println("保存失敗。停止しました。"); ready=false; }
  }
  if (!order.attempt(now)) return;
  Serial.print("注文API: HTTP "); Serial.println(order.reply().httpStatus);
  if (order.state()==koroai::State::Accepted) {
    Serial.println("受付済み。決済・配送の完了ではありません。");
    ready=order.acknowledge();
    nextPressAt=WiFi.getTime()+60;
  } else if (order.state()==koroai::State::Blocked || order.state()==koroai::State::StorageError) {
    Serial.println("注文結果と設定の確認が必要です。停止しました。"); ready=false;
  }
}
