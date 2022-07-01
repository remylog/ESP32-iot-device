#include "DHT.h"
#include <WiFi.h>
extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/timers.h"
}
#include <AsyncMqttClient.h>

#define WIFI_SSID "aterm-f9e656"
#define WIFI_PASSWORD "3dbc77999c566"

// Windows Mosquitto MQTT ブローカーのノード レッド
//#define MQTT_HOST IPAddress(192, 168, 29, 33)
//クラウド MQTT ブローカーの場合は、ドメイン名を入力します。
#define MQTT_HOST "mqtt.iot.remylog.com"
#define MQTT_PORT 1883

// 温度 MQTT トピックス
#define MQTT_PUB_TEMP "temperature"//esp32/dhtReadmqttdata/
#define MQTT_PUB_HUM  "humidity"//esp32/dhtReadmqttdata/
#define MQTT_PUB_HIN "heatindex"//esp32/dhtReadmqttdata/
#define MQTT_PUB_HINL "heatlevel"//esp32/dhtReadmqttdata/

// DHTセンサーに接続されたデジタルピン
#define DHTPIN 22  

// 使用しているDHTセンサーの種類のコメントを解除する
#define DHTTYPE DHT11   // DHT 11
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)   

// DHTセンサーの初期化
DHT dht(DHTPIN, DHTTYPE);

// センサーの読み取り値を保持する変数
float temp;
float hum;
float hin;
float hinl;

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

unsigned long previousMillis = 0;   // 温度が最後に公開された時刻のストア
const long interval = 3000;        // センサーの読み取り値を公開する間隔

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void WiFiEvent(WiFiEvent_t event) {
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      connectToMqtt();
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      xTimerStop(mqttReconnectTimer, 0); // Wi-Fi への再接続中に MQTT に再接続しないようにする
      xTimerStart(wifiReconnectTimer, 0);
      break;
  }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

/*void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}
void onMqttUnsubscribe(uint16_t packetId) {
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}*/

void onMqttPublish(uint16_t packetId) {
  Serial.print("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  dht.begin();
  
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  //mqttClient.onSubscribe(onMqttSubscribe);
  //mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  // Wi-Fi への再接続中に MQTT に再接続しないようにする
  //mqttClient.setCredentials("REPlACE_WITH_YOUR_USER", "REPLACE_WITH_YOUR_PASSWORD");
  connectToWifi();
}

void loop() {
  unsigned long currentMillis = millis();
  // X 秒単位 (間隔 = 3 秒)
  // 新しい MQTT メッセージをパブリッシュします
  if (currentMillis - previousMillis >= interval) {
    // 新しい読み取り値が最後に公開された時刻を保存する
    previousMillis = currentMillis;
    // 新しいDHTセンサーの読み取り値
    hum = dht.readHumidity();
    // 温度を摂氏として読み取ります(デフォルト)
    temp = dht.readTemperature();
    // 温度を華氏として読み取ります (isFahrenheit = true)
    //temp = dht.readTemperature(true);
    // 温度と湿度から暑さ指数を求めます
     hin = (-8.76939+0.14775*hum+0.90739*temp);
    //暑さ指数に応じて暑さ危険度レベルに変換
     if (hin>=31){
      hinl = 5.0;
     }
     else if (hin>=28){
      hinl = 4.0;
     }
     else if (hin>=25){
      hinl = 3.0;
     }
     else if (hin>=21){
      hinl = 2.0; 
     }
     else {
      hinl = 1.0;
     }

    // 読み取りが失敗したかどうかを確認し、早めに終了します (再試行するため)。
    if (isnan(temp) || isnan(hum)) {
      Serial.println(F("Failed to read from DHT sensor!"));
      return;
    }
    
    // トピック esp32/dht/temperature に関する MQTT メッセージのパブリッシュ
    uint16_t packetIdPub1 = mqttClient.publish(MQTT_PUB_TEMP, 1, true, String(temp).c_str());                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", MQTT_PUB_TEMP, packetIdPub1);
    Serial.printf("Message: %.2f \n", temp);

    // トピック esp32/dht/湿度に関する MQTT メッセージのパブリッシュ
    uint16_t packetIdPub2 = mqttClient.publish(MQTT_PUB_HUM, 1, true, String(hum).c_str());                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId %i: ", MQTT_PUB_HUM, packetIdPub2);
    Serial.printf("Message: %.2f \n", hum);

   // トピック esp32/dht/暑さ指数に関する MQTT メッセージのパブリッシュ
    uint16_t packetIdPub3 = mqttClient.publish(MQTT_PUB_HIN, 1, true, String(hin).c_str());                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId %i: ", MQTT_PUB_HIN, packetIdPub3);
    Serial.printf("Message: %.2f \n", hin);

    // トピック esp32/dht/暑さ指数レベルに関する MQTT メッセージのパブリッシュ
    uint16_t packetIdPub4 = mqttClient.publish(MQTT_PUB_HINL, 1, true, String(hinl).c_str());                            
    Serial.printf("Publishing on topic %s at QoS 1, packetId %i: ", MQTT_PUB_HINL, packetIdPub4);
    Serial.printf("Message: %.2f \n", hinl);
    
  }
}
