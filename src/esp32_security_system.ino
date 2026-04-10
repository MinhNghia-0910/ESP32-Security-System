#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#define PIR_PIN 27
#define LED_PIN 14
#define BUZZER_PIN 25
#define I2C_SDA 33
#define I2C_SCL 32

LiquidCrystal_PCF8574 lcd(0x27);

const char* ssid = "*****";
const char* password = "*****";

const char* mqtt_server = "******************.s1.eu.hivemq.cloud";
const int   mqtt_port = 8883;
const char* mqtt_username = "*****";
const char* mqtt_password = "********";

WiFiClientSecure mqttSecure;
PubSubClient client(mqttSecure);

const char* TOPIC_PIR   = "security/pir";
const char* TOPIC_EVENT = "security/event";
const char* TOPIC_CMD   = "security/cmd";     

#define BOT_TOKEN "NHAP_BOT_TOKEN_CUA_BAN"
#define CHAT_ID   "NHAP_CHAT_ID_CUA_BAN"

WiFiClientSecure tgSecure;
UniversalTelegramBot bot(BOT_TOKEN, tgSecure);

unsigned long lastTelegramSent = 0;
const unsigned long telegramInterval = 60UL * 1000UL; 
bool pendingTelegramAlert = false;

bool systemEnabled = true;
int pirState = LOW;
int lastPirState = LOW;

unsigned long motionStart = 0;
bool isMotionActive = false;

unsigned long lastBlink = 0;
bool blinkState = false;

const unsigned long ALARM_WINDOW_MS = 5000;
const unsigned long BLINK_MS = 200;

void lcdPrint(const char* l1, const char* l2) {
  lcd.setCursor(0, 0); lcd.print("                ");
  lcd.setCursor(0, 0); lcd.print(l1);
  lcd.setCursor(0, 1); lcd.print("                ");
  lcd.setCursor(0, 1); lcd.print(l2);
}

void showSystemReady() {
  if (systemEnabled) {
    lcdPrint("SECURITY: ON", "Waiting...");
  } else {
    lcdPrint("SECURITY: OFF", "Disabled");
  }
}

void hardStopOutputs() {
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  blinkState = false;
}

void printStatusSerial() {
  Serial.print("Trạng thái hệ thống: ");
  Serial.print(systemEnabled ? "Hệ thống đang BẬT" : "Hệ thống đang TẮT");
  Serial.print(" - Trạng thái cảm biến: ");
  Serial.print(pirState == HIGH ? "Có chuyển động" : "Không có chuyển động");
  Serial.print(" - LED: ");
  Serial.print(digitalRead(LED_PIN) ? "Sáng" : "Tắt");
  Serial.print(" - Còi báo động: ");
  Serial.println(digitalRead(BUZZER_PIN) ? "Đang kêu" : "Tắt");
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Đang kết nối WiFi... ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nKết nối WiFi thành công!");
  Serial.print("Địa chỉ IP: ");
  Serial.println(WiFi.localIP());
}

void telegramMotionAlert() {
  if (String(BOT_TOKEN) == "" || String(CHAT_ID) == "") return;

  unsigned long now = millis();
  if (now - lastTelegramSent < telegramInterval) return;

  if (bot.sendMessage(CHAT_ID, "CẢNH BÁO: Phát hiện chuyển động!", "")) {
    lastTelegramSent = now;
    Serial.println("Đã gửi Telegram: CẢNH BÁO: Phát hiện chuyển động!");
  } else {
    Serial.println("Lỗi: Không thể gửi Telegram.");
  }
}

void applyCommand(const String& cmdUpper) {
  if (cmdUpper == "ON") {
    systemEnabled = true;

    isMotionActive = false;
    motionStart = 0;
    hardStopOutputs();

    pirState = digitalRead(PIR_PIN);
    lastPirState = pirState;

    lcdPrint("SYSTEM ENABLED", "READY");
    Serial.println("Hệ thống đã được BẬT qua Node-RED");

    showSystemReady();
  }
  else if (cmdUpper == "OFF") {
    systemEnabled = false;

    hardStopOutputs();
    isMotionActive = false;
    motionStart = 0;

    lcdPrint("SYSTEM DISABLED", "DISABLED");
    Serial.println("Hệ thống đã được TẮT qua Node-RED");
  }
  else if (cmdUpper == "STATUS") {
    printStatusSerial();
  }
  else {
    Serial.println("Lệnh CMD không hợp lệ (use: ON/OFF/STATUS).");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String cmd;
  for (unsigned int i = 0; i < length; i++) cmd += (char)payload[i];
  cmd.trim();
  cmd.toUpperCase();

  Serial.print("CMD received (MQTT): ");
  Serial.println(cmd);

  applyCommand(cmd);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Đang kết nối MQTT... ");
    String clientId = "ESP32-SEC-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("OK");
      client.subscribe(TOPIC_CMD);
      Serial.print("Subscribed: ");
      Serial.println(TOPIC_CMD);
      lcdPrint("System Ready", "MQTT Connected");
      delay(200);
      showSystemReady();
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry in 5s");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Hệ thống khởi động...");

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  hardStopOutputs();

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.begin(16, 2);
  lcd.setBacklight(255);
  lcdPrint("System Ready", "Connecting WiFi");

  setup_wifi();

  mqttSecure.setInsecure();
  tgSecure.setInsecure();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  lcdPrint("System Ready", "Connecting MQTT");
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  if (!systemEnabled) {
    hardStopOutputs();
    delay(10);
    return;
  }

  pirState = digitalRead(PIR_PIN);

  if (pirState != lastPirState) {
    client.publish(TOPIC_PIR, pirState == HIGH ? "1" : "0");

    if (pirState == HIGH) {
      Serial.println("PIR: HIGH - Có chuyển động");
    } else {
      Serial.println("PIR: LOW - Không có chuyển động");
    }

    if (lastPirState == LOW && pirState == HIGH) {
      client.publish(TOPIC_EVENT, "MOTION");
      Serial.println("Đã gửi MQTT event: MOTION");

      motionStart = millis();
      isMotionActive = true;

      pendingTelegramAlert = true;
    }

    lastPirState = pirState;
  }

  if (pirState == HIGH) {
    motionStart = millis();
    isMotionActive = true;
  }

  if (isMotionActive && (millis() - motionStart < ALARM_WINDOW_MS)) {
    if (millis() - lastBlink >= BLINK_MS) {
      lastBlink = millis();
      blinkState = !blinkState;
      digitalWrite(LED_PIN, blinkState ? HIGH : LOW);
      digitalWrite(BUZZER_PIN, blinkState ? HIGH : LOW);
    }
    lcdPrint("Motion Detected", "ALERT!!!");
  } else {
    isMotionActive = false;
    hardStopOutputs();
    lcdPrint("No Motion", "System Ready");
  }

  if (pendingTelegramAlert) {
    pendingTelegramAlert = false;
    telegramMotionAlert();
  }

  delay(10);
}