#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>


// MQTT
const char* mqtt_host = "mqtt.lunka.io";
const int mqtt_port = 8883;

const char* mqtt_client_id = "ba12512b-6575-44f1-8b09-fd26eea66817";
const char* mqtt_username = "2044c6e0-8890-48f6-9ff2-2c4941b824b5";
const char* mqtt_password = "ccc9ec2b-1359-4370-b9db-0003bf098b5e";

const char* topic_temperature = "groupA/temperature";
const char* topic_humidity = "groupA/humidity";
const char* topic_command = "cmd/digital";
const char* topic_relay = "ba12512b-6575-44f1-8b09-fd26eea66817/pin/19/state";


// ================= DHT22 =================

#define DHTPIN 32 
#define DHTTYPE DHT22
DHT dht(DHTPIN,DHTTYPE);

float temperature = NAN;
float humidity = NAN;

unsigned long dhtTimer=0;


// ================= Soil =================

#define sensor_pin 33  // soil 
int sensor_analog;
float moisture;


// ================= OLED =================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH,SCREEN_HEIGHT,&Wire,-1);


// ================= LED =================

#define LED1_PIN 14
#define LED2_PIN 12
#define LED3_PIN 13

int ledPins[]={
LED1_PIN,
LED2_PIN,
LED3_PIN
};

int ledCount=3;
int ledIndex=0;

unsigned long previousLedMillis=0;

const unsigned long ledInterval=200;


// ================= Relay =================

#define RELAY_PIN 19
#define BUTTON_PIN 18


bool relayState=false;
unsigned long lastButtonPress=0;
const unsigned long debounceDelay=200;


// ===== Auto Pump Condition =====

#define NORMAL_TEMP 26.0
#define NORMAL_HUMI 60.0

bool autoPump=true;
bool manualControl=false;

// เวลา Manual Control
unsigned long manualStartTime = 0;
const unsigned long manualTimeout = 60000; 
// 60 วินาที

// ================= WIFI RESET =================

#define WIFI_RESET_PIN 17

const unsigned long wifiResetHoldTime=3000;
unsigned long wifiResetPressMillis=0;
bool wifiResetInProgress=false;

WiFiClientSecure net;
PubSubClient client(net);

void setupWiFi()
{
  WiFiManager wm;
  wm.setConnectTimeout(10);  // รอ 10 วินาที
  if (!wm.autoConnect("AP_IOT_setup")) {
    Serial.println("No WiFi - Run Offline Mode");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    return;
  }
  Serial.println("Wi-Fi Connected! SSID: " + WiFi.SSID());
}


void reconnectMQTT() {
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect(mqtt_client_id, mqtt_username, mqtt_password)) {
      Serial.println("MQTT Connected!");
      client.subscribe(topic_command);
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 seconds...");
      delay(5000);
    }
  }
}

void sendSensorData(float temperature, float humidity, float moisture) {
  if (!isnan(temperature) && !isnan(humidity)) {
    String payload = "{\"temp\":";
    payload += String(temperature, 2);
    payload += ",\"humi\":";
    payload += String(humidity, 2);
    payload += ",\"soil\":";
    payload += String(moisture, 2);
    payload += "}";
    client.publish("db/write", payload.c_str());
    Serial.printf("Sensor JSON Sent: %s\n", payload.c_str());
  } else {
    Serial.println("Invalid sensor readings!");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];


  Serial.println("---- [DEBUG] MQTT Callback ----");
  Serial.print("[MQTT RECEIVE] topic: ");
  Serial.println(topic);
  Serial.print("[MQTT RECEIVE] payload: ");
  Serial.println(message);

  if (String(topic) == topic_command) {
    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      Serial.print("[DEBUG][ERROR] JSON Parse fail: ");
      Serial.println(error.c_str());
      return;
    }

    const char* pin = doc["pin"];
    int value = doc["value"];
    Serial.print("[DEBUG] JSON pin: ");
    Serial.println(pin);
    Serial.print("[DEBUG] JSON value: ");
    Serial.println(value);

    if (String(pin) == "19") {
      if (value == 1) {
        digitalWrite(RELAY_PIN, HIGH);
        relayState = true;
        Serial.println("[DEBUG] Relay ON by MQTT JSON");
      } else if (value == 0) {
        digitalWrite(RELAY_PIN, LOW);
        relayState = false;
        Serial.println("[DEBUG] Relay OFF by MQTT JSON");
      } else {
        Serial.println("[DEBUG][WARN] Unknown value for relay!");
      }
    } else {
      Serial.print("[DEBUG][WARN] Pin mismatch! JSON pin = ");
      Serial.println(pin);
    }


    char buffer[2];
    sprintf(buffer, "%d", relayState ? 1 : 0);
    Serial.print("[DEBUG] Publish state to MQTT: ");
    Serial.print(topic_relay);
    Serial.print(" payload: ");
    Serial.println(buffer);
    client.publish(topic_relay, buffer);
  }
  Serial.println("------ END DEBUG ------");
}

void setup() {

Serial.begin(115200);
dht.begin();
delay(2000);

if(!display.begin(SSD1306_PAGEADDR,0x3C)){
Serial.println("OLED Error");

while(true);

}


display.clearDisplay();
display.setTextColor(SSD1306_WHITE);

pinMode(LED1_PIN,OUTPUT);
pinMode(LED2_PIN,OUTPUT);
pinMode(LED3_PIN,OUTPUT);


for(int i=0;i<ledCount;i++)
{
digitalWrite(ledPins[i],LOW);
}

pinMode(RELAY_PIN,OUTPUT);
pinMode(BUTTON_PIN,INPUT_PULLUP);
pinMode(WIFI_RESET_PIN,INPUT);
digitalWrite(RELAY_PIN,LOW);

setupWiFi();

net.setInsecure();
client.setServer(mqtt_host,mqtt_port);
client.setCallback(mqttCallback);

}

unsigned long time_count=0;
unsigned long timer=60000;
int cc = 0;


void loop()
{
//================ WIFI RESET =================
  if (digitalRead(WIFI_RESET_PIN) == HIGH) {
    if (!wifiResetInProgress) {
      wifiResetPressMillis = millis();
      wifiResetInProgress = true;
      Serial.println("[WIFI RESET] Button pressed, waiting...");
    } else if (millis() - wifiResetPressMillis > wifiResetHoldTime) {
      Serial.println("[WIFI RESET] Hold detected, resetting WiFi...");
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("[WIFI RESET] WiFiManager reset! Rebooting...");
      delay(2000);
      ESP.restart();
    }
  } else {
    wifiResetInProgress = false;
  }

//================ MQTT =================

  // if (!client.connected()) {
  //   reconnectMQTT();
  // }
  // client.loop();

if(WiFi.status()==WL_CONNECTED)
{
  if(!client.connected())
  {
    reconnectMQTT();
  }
  client.loop();
}

//================ DHT22 & Soil =================
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  
  sensor_analog= analogRead(sensor_pin);
  //moisture = sensor_analog;
  moisture = ((4095.00 - sensor_analog)/(4095.00-500)) * 100; 

  if(moisture > 100)
  moisture = 100;
  if(moisture < 0)
  moisture = 0;

  Serial.print(sensor_analog);
  Serial.print(":");
  Serial.print(moisture);  /* Print Temperature on the serial window */
  Serial.println("%");
  delay(1000); 


//================ AUTO PUMP =================

if(autoPump==true  && manualControl==false && !isnan(temperature) && !isnan(humidity))
{

  if(temperature > NORMAL_TEMP && humidity < NORMAL_HUMI)
  {
  digitalWrite(RELAY_PIN,HIGH);
  relayState=true;
  Serial.println("Auto PUMP ON");
  }
  else
  {
  digitalWrite(RELAY_PIN,LOW);
  relayState=false;
  Serial.println("Auto PUMP OFF");
  }

}


//================ LED RUN =================

  if (millis() - previousLedMillis >= ledInterval) {
    previousLedMillis = millis();
    for (int i = 0; i < ledCount; i++) {
      digitalWrite(ledPins[i], LOW);
    }
    digitalWrite(ledPins[ledIndex], HIGH);
    ledIndex++;
    if (ledIndex >= ledCount) ledIndex = 0;
  }
//================ Manual Button =================

  if (digitalRead(BUTTON_PIN) == HIGH) {   // LOW = กด (pullup logic)
    unsigned long now = millis();
    if (now - lastButtonPress > debounceDelay) {
      // เริ่มจับเวลา
      manualControl = true;
      manualStartTime = millis();

      relayState = !relayState;
      digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
      Serial.print("Manual Pump : ");

      char buffer[2];
      sprintf(buffer, "%d", relayState ? 1 : 0);
      client.publish(topic_relay, buffer);

      lastButtonPress = now;
    }
  }

//================ Manual Timeout =================


if(manualControl == true)
{
  if(millis() - manualStartTime >= manualTimeout)
  {
    manualControl = false;
    Serial.println("Manual Timeout -> AUTO MODE");
  }
}

//================ OLED =================


display.clearDisplay();
display.setCursor(0,0);
display.setTextSize(1);
display.print("Temp:");
display.setTextSize(2);
display.print(temperature,1);
display.println("C");
display.setTextSize(1);
display.print("Humi:");
display.setTextSize(2);
display.print(humidity,1);
display.println("%");
display.setTextSize(1);
display.print("Soil:");
display.print(moisture,1);
display.println("%");
display.print("Pump:");

if(relayState)

display.println("ON");

else

display.println("OFF");
display.print("CAMT Smart Farm"); // แสดงผลข้อความ
display.display();


//================ MQTT Send =================

if(millis()-time_count>=timer){
sendSensorData(temperature,humidity,moisture);
time_count=millis();
}

}