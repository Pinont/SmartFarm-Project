#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// ================= MQTT =================

// =============== DEFAULT ================

// const char* mqtt_host = "mqtt.lunka.io"; 
// const int mqtt_port = 8883; 

// const char* mqtt_client_id = "ba12512b-6575-44f1-8b09-fd26eea66817";
// const char* mqtt_username = "2044c6e0-8890-48f6-9ff2-2c4941b824b5";
// const char* mqtt_password = "ccc9ec2b-1359-4370-b9db-0003bf098b5e";

// ================ LOCAL =================

const char* mqtt_host = "10.89.34.119";       // reCamera IP
const int mqtt_port = 1883;                 // Plain MQTT port

const char* mqtt_client_id = "esp32_smartfarm_relay";
const char* mqtt_username = "eda5a529-32d5-4670-a655-9caa55c804b2";
const char* mqtt_password = "8eca69f3-a041-48aa-b0d6-16992487bb9b";

// Topics
const char* topic_temperature = "groupA/temperature";
const char* topic_humidity = "groupA/humidity";
const char* topic_command = "cmd/digital";
const char* topic_relay = "ba12512b-6575-44f1-8b09-fd26eea66817/pin/19/state";

// reCamera topics (2-class system)
const char* topic_plant_stage = "smartfarm/plant_stage";
const char* topic_water_mode = "smartfarm/water_mode";
const char* topic_harvest_alert = "smartfarm/harvest_alert";

// ================= DHT22 =================
#define DHTPIN 32
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
float temperature = NAN;
float humidity = NAN;
unsigned long dhtTimer = 0;

// ================= Soil Moisture =================
#define SENSOR_PIN 33
int sensor_analog;
float moisture;

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= LEDs =================
#define LED1_PIN 14
#define LED2_PIN 12
#define LED3_PIN 13
#define AUTO_BUTTON_PIN 5
int ledPins[] = { LED1_PIN, LED2_PIN, LED3_PIN };
int ledCount = 3;
int ledIndex = 0;
unsigned long previousLedMillis = 0;
const unsigned long ledInterval = 200;

// ================= Relay =================
#define RELAY_PIN 19
#define BUTTON_PIN 18
bool relayState = false;
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;

// ================= Pump Control (2-Stage) =================
float onThreshold = 30;
float offThreshold = 70;
bool autoPump = true;
bool manualControl = false;
unsigned long manualStartTime = 0;
const unsigned long manualTimeout = 60000; // 60s

// reCamera integration state
int plant_stage = 0;        // 0=None, 1=Vegetative, 2=Harvest
String waterMode = "idle";  // "water", "harvest", "idle"
bool harvestAlert = false;

// ================= WiFi Reset =================
#define WIFI_RESET_PIN 17
const unsigned long wifiResetHoldTime = 3000;
unsigned long wifiResetPressMillis = 0;
bool wifiResetInProgress = false;

// ================= Network =================
WiFiClient net;
PubSubClient client(net);

// ================= Forward Declarations =================
void setupWiFi();
void reconnectMQTT();
void callback(char* topic, byte* payload, unsigned int length);
void sendSensorData(float t, float h, float m);
void applyWaterMode(String mode);
void controlPump(float m);
void updateDisplay();
void handleButton();
void handleWiFiReset();
void heartbeatLED();
void handleAutoPump();

void setup() {
  Serial.begin(115200);
  delay(100);

  // Pins
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  pinMode(AUTO_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);

  for (int i = 0; i < ledCount; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  // Sensors
  dht.begin();
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("SmartFarm v2.0");
  display.println("2-Stage: Veg/Harvest");
  display.display();

  // MQTT
  // net.setInsecure(); // for testing; replace with CA cert in production
  client.setServer(mqtt_host, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);
  client.setSocketTimeout(10);

  setupWiFi();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    updateDisplay();
    if (!client.connected()) {
      reconnectMQTT();
    }
    client.loop();
  }

  // Sensor readings (every 2s)
  if (millis() - dhtTimer > 2000) {
    dhtTimer = millis();
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    sensor_analog = analogRead(SENSOR_PIN);
    moisture = 100.0 - ((sensor_analog / 4095.0) * 100.0);

    sendSensorData(temperature, humidity, moisture);
    updateDisplay();
  }

  // Auto pump control
  if (autoPump && !manualControl && !harvestAlert) {
    controlPump(moisture);
  }

  // Manual control timeout
  if (manualControl && millis() - manualStartTime > manualTimeout) {
    manualControl = false;
    Serial.println("Manual control timeout -> auto");
  }

  // UI
  handleButton();
  handleAutoPump();
  handleWiFiReset();
  heartbeatLED();
}

// ================= WiFi =================
void setupWiFi() {
  WiFiManager wm;
  wm.setConnectTimeout(10);
  if (!wm.autoConnect("AP_IOT_setup")) {
    Serial.println("No WiFi - Offline Mode");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    return;
  }
  Serial.println("Wi-Fi Connected: " + WiFi.SSID());
}

// ================= MQTT =================
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("MQTT connecting...");
    if (client.connect(mqtt_client_id, mqtt_username, mqtt_password)) {
      Serial.println(" OK");
      client.subscribe(topic_command);
      client.subscribe(topic_plant_stage);
      client.subscribe(topic_water_mode);
      client.subscribe(topic_harvest_alert);
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry 5s");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("MQTT [%s]: %s\n", topic, msg.c_str());

  if (String(topic) == topic_command) {
    // Manual relay command: {"pin":19,"value":1}
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      int pin = doc["pin"] | -1;
      int val = doc["value"] | -1;
      if (pin == RELAY_PIN && (val == 0 || val == 1)) {
        manualControl = true;
        manualStartTime = millis();
        relayState = (val == 1);
        digitalWrite(RELAY_PIN, relayState);
        Serial.printf("Manual relay: %d\n", relayState);
      }
    }
  }
  else if (String(topic) == topic_plant_stage) {
    // {"plant_stage":1,"harvest_alert":false,"water_mode":"water",...}
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      plant_stage = doc["plant_stage"] | 0;
      harvestAlert = doc["harvest_alert"] | false;
      waterMode = doc["water_mode"] | "idle";
      applyWaterMode(waterMode);
    }
  }
  else if (String(topic) == topic_water_mode) {
    // Simple string: "water" | "harvest" | "idle"
    waterMode = msg;
    applyWaterMode(waterMode);
  }
  else if (String(topic) == topic_harvest_alert) {
    // "true" / "false"
    harvestAlert = (msg == "true" || msg == "1");
    if (harvestAlert) {
      relayState = false;
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("HARVEST ALERT -> Pump OFF");
    }
  }
}

// ================= Water Mode Application =================
void applyWaterMode(String mode) {
  waterMode = mode;
  if (mode == "water") {
    // Vegetative: normal irrigation
    onThreshold = 30;
    offThreshold = 70;
    autoPump = true;
  } else if (mode == "harvest") {
    // Harvest: stop irrigation
    onThreshold = 101; // Never triggers
    offThreshold = 101;
    autoPump = false;
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);
  } else {
    // Idle/None: conservative
    onThreshold = 25;
    offThreshold = 65;
    autoPump = true;
  }
  Serial.printf("Water mode: %s | Thresholds: ON=%.0f OFF=%.0f\n",
                waterMode.c_str(), onThreshold, offThreshold);
}

// ================= Pump Control =================
void controlPump(float m) {
  if (isnan(m)) return;
  if (m <= onThreshold && !relayState) {
    relayState = true;
    digitalWrite(RELAY_PIN, HIGH);
    Serial.printf("Pump ON (moisture=%.1f)\n", m);
  } else if (m >= offThreshold && relayState) {
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);
    Serial.printf("Pump OFF (moisture=%.1f)\n", m);
  }
}

// ================= Handle Auto Pump ==============
void handleAutoPump() {
  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastDebounceTime = 0;

  bool reading = digitalRead(AUTO_BUTTON_PIN);

  // Raw state changed
  if (reading != lastReading) {
    lastDebounceTime = millis();
    lastReading = reading;
  }

  // Debounce
  if (millis() - lastDebounceTime >= debounceDelay) {

    // Stable state changed
    if (reading != stableState) {
      stableState = reading;

      // Button pressed
      if (stableState == LOW) {

        autoPump = !autoPump;

        Serial.print("Auto Pump -> ");
        Serial.println(autoPump ? "ON" : "OFF");
      }
    }
  }
}

// ================= Sensor Publish =================
void sendSensorData(float t, float h, float m) {
  if (isnan(t) || isnan(h)) return;
  String payload = "{\"temp\":";
  payload += String(t, 2);
  payload += ",\"humi\":";
  payload += String(h, 2);
  payload += ",\"soil\":";
  payload += String(m, 2);
  payload += "}";
  client.publish("db/write", payload.c_str());
}

// ================= OLED =================
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);

  // Stage
  display.print("Stage: ");
  if (plant_stage == 1) display.println("VEGETATIVE");
  else if (plant_stage == 2) display.println("HARVEST");
  else display.println("NONE");

  // Water mode
  display.print("Mode: ");
  display.println(waterMode);

  // Harvest alert
  if (harvestAlert) {
    display.setTextColor(BLACK, WHITE); // Inverted
    display.println(">>> HARVEST <<<");
    display.setTextColor(WHITE);
  }

  // Sensors
  display.printf("T:%.1f H:%.1f\n", temperature, humidity);
  display.printf("Soil: %.1f%%\n", moisture);
  display.printf("Pump: %s\n", relayState ? "ON" : "OFF");
  display.printf("Auto: %s\n", autoPump ? "ON" : "OFF");

  display.display();
}

// ================= Button (Manual Toggle) =================
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading == LOW && millis() - lastButtonPress > debounceDelay) {
    lastButtonPress = millis();
    manualControl = true;
    manualStartTime = millis();
    relayState = !relayState;
    digitalWrite(RELAY_PIN, relayState);
    Serial.printf("Button -> Manual Pump: %s\n", relayState ? "ON" : "OFF");
  }
}

// ================= WiFi Reset (Long Press) ================= 
// void handleWiFiReset() { --> old code
//   int reading = digitalRead(WIFI_RESET_PIN);
//   if (reading == LOW) {
//     if (!wifiResetInProgress) {
//       wifiResetPressMillis = millis();
//       wifiResetInProgress = true;
//     } else if (millis() - wifiResetPressMillis > wifiResetHoldTime) {
//       Serial.println("WiFi reset triggered");
//       WiFiManager wm;
//       wm.resetSettings();
//       ESP.restart();
//     }
//   } else {
//     wifiResetInProgress = false;
//   }
// }

void handleWiFiReset() { // --> new code
  static int lastReading = HIGH;
  static unsigned long lowStart = 0;

  int reading = digitalRead(WIFI_RESET_PIN);

  // Detect transition
  if (reading != lastReading) {
    Serial.printf(
      "[GPIO17] %s at %lu ms\n",
      reading == LOW ? "LOW" : "HIGH",
      millis()
    );

    if (reading == LOW) {
      lowStart = millis();
    } else {
      if (lowStart != 0) {
        Serial.printf(
          "[GPIO17] LOW duration: %lu ms\n",
          millis() - lowStart
        );
      }

      lowStart = 0;
    }

    lastReading = reading;
  }

  // Long press detection
  if (reading == LOW &&
      lowStart != 0 &&
      millis() - lowStart >= wifiResetHoldTime) {

    Serial.printf(
      "[GPIO17] LOW for %lu ms -> RESET\n",
      millis() - lowStart
    );

    WiFiManager wm;
    wm.resetSettings();

    delay(500);
    ESP.restart();
  }
}

// ================= Heartbeat LED =================
void heartbeatLED() {
  if (millis() - previousLedMillis >= ledInterval) {
    previousLedMillis = millis();
    for (int i = 0; i < ledCount; i++) {
      digitalWrite(ledPins[i], LOW);
    }
    digitalWrite(ledPins[ledIndex], HIGH);
    ledIndex = (ledIndex + 1) % ledCount;
  }
}