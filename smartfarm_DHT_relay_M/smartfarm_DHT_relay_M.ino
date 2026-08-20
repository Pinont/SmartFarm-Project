#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// ============================================================
// MQTT
// ============================================================

const char* mqtt_host = "10.89.34.119";
const int mqtt_port = 1883;

const char* mqtt_client_id = "esp32_smartfarm_relay";
const char* mqtt_username = "eda5a529-32d5-4670-a655-9caa55c804b2";
const char* mqtt_password = "8eca69f3-a041-48aa-b0d6-16992487bb9b";

// MQTT topics
const char* topic_temperature = "groupA/temperature";
const char* topic_humidity = "groupA/humidity";

const char* topic_command = "cmd/digital";

const char* topic_relay =
    "ba12512b-6575-44f1-8b09-fd26eea66817/pin/19/state";

// reCamera
const char* topic_plant_stage = "smartfarm/plant_stage";
const char* topic_water_mode = "smartfarm/water_mode";
const char* topic_harvest_alert = "smartfarm/harvest_alert";

// ============================================================
// DHT22
// ============================================================

#define DHTPIN 32
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

float temperature = NAN;
float humidity = NAN;

unsigned long dhtTimer = 0;

// ============================================================
// Soil Moisture
// ============================================================

#define SENSOR_PIN 33

int sensor_analog = 0;
float moisture = 0;

// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1
);

// ============================================================
// LEDs
// ============================================================

#define LED1_PIN 14
#define LED2_PIN 12
#define LED3_PIN 13

int ledPins[] = {
    LED1_PIN,
    LED2_PIN,
    LED3_PIN
};

const int ledCount = 3;

int ledIndex = 0;

unsigned long previousLedMillis = 0;

const unsigned long ledInterval = 200;

// ============================================================
// Relay / Manual Button
// ============================================================

#define RELAY_PIN 19
#define BUTTON_PIN 18

bool relayState = false;

unsigned long lastButtonPress = 0;

const unsigned long debounceDelay = 200;

// Manual mode timeout
bool manualControl = false;

unsigned long manualStartTime = 0;

const unsigned long manualTimeout = 60000;

// ============================================================
// Auto Pump Button
// ============================================================

#define AUTO_BUTTON_PIN 5

// IMPORTANT:
// GPIO5 is only responsible for toggling autoPump.
// reCamera does NOT change autoPump.

bool autoPump = true;

// ============================================================
// Pump Control
// ============================================================

float onThreshold = 30;
float offThreshold = 70;

// ============================================================
// reCamera State
// ============================================================

// 0 = None
// 1 = Vegetative
// 2 = Harvest

int plant_stage = 0;

String waterMode = "idle";

bool harvestAlert = false;

// ============================================================
// Wi-Fi Reset
// ============================================================

#define WIFI_RESET_PIN 17

const unsigned long wifiResetHoldTime = 3000;

// ============================================================
// Network
// ============================================================

WiFiClient net;

PubSubClient client(net);

// ============================================================
// Function declarations
// ============================================================

void setupWiFi();

void reconnectMQTT();

void callback(
    char* topic,
    byte* payload,
    unsigned int length
);

void sendSensorData(
    float t,
    float h,
    float m
);

void applyWaterMode(String mode);

void controlPump(float m);

void updateDisplay();

void handleButton();

void handleAutoPump();

void handleWiFiReset();

void heartbeatLED();

void publishRelayState();

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    delay(500);

    Serial.println();
    Serial.println("================================");
    Serial.println(" SmartFarm ESP32");
    Serial.println("================================");

    // --------------------------------------------------------
    // Pins
    // --------------------------------------------------------

    pinMode(RELAY_PIN, OUTPUT);

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    pinMode(AUTO_BUTTON_PIN, INPUT_PULLUP);

    pinMode(WIFI_RESET_PIN, INPUT_PULLUP);

    // Pump OFF at startup
    relayState = false;

    digitalWrite(RELAY_PIN, LOW);

    // LEDs
    for (int i = 0; i < ledCount; i++) {

        pinMode(ledPins[i], OUTPUT);

        digitalWrite(ledPins[i], LOW);
    }

    // --------------------------------------------------------
    // Sensors
    // --------------------------------------------------------

    dht.begin();

    // --------------------------------------------------------
    // OLED
    // --------------------------------------------------------

    Wire.begin();

    if (!display.begin(
            SSD1306_SWITCHCAPVCC,
            0x3C
        )) {

        Serial.println(
            "SSD1306 allocation failed"
        );

    } else {

        display.clearDisplay();

        display.setTextSize(1);

        display.setTextColor(WHITE);

        display.setCursor(0, 0);

        display.println("SmartFarm v2.0");

        display.println("2-Stage");

        display.println();

        display.println("Starting...");

        display.display();
    }

    // --------------------------------------------------------
    // MQTT
    // --------------------------------------------------------

    client.setServer(
        mqtt_host,
        mqtt_port
    );

    client.setCallback(callback);

    client.setKeepAlive(60);

    client.setSocketTimeout(10);

    // --------------------------------------------------------
    // Wi-Fi
    // --------------------------------------------------------

    setupWiFi();

    Serial.println();
    Serial.println("System ready.");
}

// ============================================================
// LOOP
// ============================================================

void loop() {

    // --------------------------------------------------------
    // Network
    // --------------------------------------------------------

    if (WiFi.status() == WL_CONNECTED) {

        if (!client.connected()) {

            reconnectMQTT();
        }

        client.loop();
    }

    // --------------------------------------------------------
    // DHT / Soil readings every 2 seconds
    // --------------------------------------------------------

    if (millis() - dhtTimer >= 2000) {

        dhtTimer = millis();

        temperature = dht.readTemperature();

        humidity = dht.readHumidity();

        sensor_analog = analogRead(SENSOR_PIN);

        moisture =
            100.0 -
            ((sensor_analog / 4095.0) * 100.0);

        sendSensorData(
            temperature,
            humidity,
            moisture
        );

        updateDisplay();
    }

    // --------------------------------------------------------
    // Automatic pump control
    // --------------------------------------------------------

    if (
        autoPump &&
        !manualControl &&
        !harvestAlert &&
        waterMode != "harvest"
    ) {

        controlPump(moisture);
    }

    // --------------------------------------------------------
    // Manual timeout
    // --------------------------------------------------------

    if (
        manualControl &&
        millis() - manualStartTime >= manualTimeout
    ) {

        manualControl = false;

        Serial.println(
            "Manual control timeout -> AUTO"
        );
    }

    // --------------------------------------------------------
    // Buttons
    // --------------------------------------------------------

    handleButton();

    handleAutoPump();

    handleWiFiReset();

    // --------------------------------------------------------
    // LEDs
    // --------------------------------------------------------

    heartbeatLED();
}

// ============================================================
// Wi-Fi
// ============================================================

void setupWiFi() {

    WiFiManager wm;

    wm.setConnectTimeout(10);

    if (!wm.autoConnect("AP_IOT_setup")) {

        Serial.println(
            "No WiFi - Offline Mode"
        );

        WiFi.disconnect();

        WiFi.mode(WIFI_OFF);

        return;
    }

    Serial.println();

    Serial.println(
        "Wi-Fi Connected: " +
        WiFi.SSID()
    );

    Serial.print("IP: ");

    Serial.println(
        WiFi.localIP()
    );
}

// ============================================================
// MQTT RECONNECT
// ============================================================

void reconnectMQTT() {

    static unsigned long lastAttempt = 0;

    // Try once every 5 seconds
    if (millis() - lastAttempt < 5000) {
        return;
    }

    lastAttempt = millis();

    Serial.print("MQTT connecting...");

    if (
        client.connect(
            mqtt_client_id,
            mqtt_username,
            mqtt_password
        )
    ) {

        Serial.println(" OK");

        // ----------------------------------------------------
        // Subscribe
        // ----------------------------------------------------

        client.subscribe(topic_command);

        client.subscribe(topic_plant_stage);

        client.subscribe(topic_water_mode);

        client.subscribe(topic_harvest_alert);

        Serial.println(
            "MQTT subscriptions active"
        );

    } else {

        Serial.print(" failed, rc=");

        Serial.println(
            client.state()
        );
    }
}

// ============================================================
// MQTT CALLBACK
// ============================================================

void callback(
    char* topic,
    byte* payload,
    unsigned int length
) {

    String msg;

    for (
        unsigned int i = 0;
        i < length;
        i++
    ) {

        msg += (char)payload[i];
    }

    Serial.printf(
        "MQTT [%s]: %s\n",
        topic,
        msg.c_str()
    );

    // ========================================================
    // MANUAL RELAY COMMAND
    // ========================================================

    if (
        String(topic) ==
        topic_command
    ) {

        DynamicJsonDocument doc(256);

        DeserializationError error =
            deserializeJson(
                doc,
                msg
            );

        if (
            error ==
            DeserializationError::Ok
        ) {

            int pin =
                doc["pin"] | -1;

            int value =
                doc["value"] | -1;

            if (
                pin == RELAY_PIN &&
                (value == 0 || value == 1)
            ) {

                manualControl = true;

                manualStartTime =
                    millis();

                relayState =
                    (value == 1);

                digitalWrite(
                    RELAY_PIN,
                    relayState
                );

                Serial.printf(
                    "MQTT -> MANUAL %s | Pump: %s\n",
                    relayState ? "ON" : "OFF",
                    relayState ? "ON" : "OFF"
                );

                publishRelayState();
            }
        }

        return;
    }

    // ========================================================
    // PLANT STAGE
    // ========================================================

    if (
        String(topic) ==
        topic_plant_stage
    ) {

        // reCamera sends:
        //
        // 0 = None
        // 1 = Vegetative
        // 2 = Harvest

        int newStage =
            msg.toInt();

        if (
            newStage >= 0 &&
            newStage <= 2
        ) {

            plant_stage =
                newStage;

            Serial.printf(
                "reCamera -> Plant Stage: %d\n",
                plant_stage
            );

            // Harvest stage is a safety condition
            if (plant_stage == 2) {

                harvestAlert = true;

                relayState = false;

                digitalWrite(
                    RELAY_PIN,
                    LOW
                );

                Serial.println(
                    "Harvest stage -> Pump OFF"
                );

                publishRelayState();
            }
        }

        return;
    }

    // ========================================================
    // WATER MODE
    // ========================================================

    if (
        String(topic) ==
        topic_water_mode
    ) {

        // reCamera sends:
        //
        // idle
        // water
        // harvest

        msg.trim();

        applyWaterMode(msg);

        return;
    }

    // ========================================================
    // HARVEST ALERT
    // ========================================================

    if (
        String(topic) ==
        topic_harvest_alert
    ) {

        msg.trim();

        harvestAlert =
            (
                msg == "true" ||
                msg == "1"
            );

        Serial.print(
            "reCamera -> Harvest Alert: "
        );

        Serial.println(
            harvestAlert
                ? "TRUE"
                : "FALSE"
        );

        if (harvestAlert) {

            relayState = false;

            digitalWrite(
                RELAY_PIN,
                LOW
            );

            Serial.println(
                "HARVEST ALERT -> Pump OFF"
            );

            publishRelayState();
        }

        return;
    }
}

// ============================================================
// WATER MODE
// ============================================================
//
// IMPORTANT:
// This function DOES NOT modify autoPump.
//
// GPIO5 is the only local control for autoPump.
//

void applyWaterMode(String mode) {

    mode.trim();

    waterMode = mode;

    // ========================================================
    // WATER
    // ========================================================

    if (mode == "water") {

        onThreshold = 30;

        offThreshold = 70;

        Serial.println(
            "Water mode = WATER"
        );
    }

    // ========================================================
    // HARVEST
    // ========================================================

    else if (mode == "harvest") {

        onThreshold = 101;

        offThreshold = 101;

        // Safety: stop pump
        relayState = false;

        digitalWrite(
            RELAY_PIN,
            LOW
        );

        Serial.println(
            "Water mode = HARVEST -> Pump OFF"
        );

        publishRelayState();
    }

    // ========================================================
    // IDLE
    // ========================================================

    else {

        waterMode = "idle";

        onThreshold = 25;

        offThreshold = 65;

        Serial.println(
            "Water mode = IDLE"
        );
    }

    // IMPORTANT:
    // autoPump is NOT changed here.

    Serial.printf(
        "Mode: %s | ON=%.0f OFF=%.0f | AutoPump=%s\n",
        waterMode.c_str(),
        onThreshold,
        offThreshold,
        autoPump ? "ON" : "OFF"
    );
}

// ============================================================
// AUTOMATIC PUMP CONTROL
// ============================================================

void controlPump(float m) {

    if (isnan(m)) {
        return;
    }

    // --------------------------------------------------------
    // Moisture too low -> Pump ON
    // --------------------------------------------------------

    if (
        m <= onThreshold &&
        !relayState
    ) {

        relayState = true;

        digitalWrite(
            RELAY_PIN,
            HIGH
        );

        Serial.printf(
            "AUTO -> Pump ON | Soil: %.1f%%\n",
            m
        );

        publishRelayState();
    }

    // --------------------------------------------------------
    // Moisture high enough -> Pump OFF
    // --------------------------------------------------------

    else if (
        m >= offThreshold &&
        relayState
    ) {

        relayState = false;

        digitalWrite(
            RELAY_PIN,
            LOW
        );

        Serial.printf(
            "AUTO -> Pump OFF | Soil: %.1f%%\n",
            m
        );

        publishRelayState();
    }
}

// ============================================================
// GPIO18 - MANUAL PUMP BUTTON
// ============================================================
//
// One physical press = one toggle.
//
// Holding the button does NOT repeatedly toggle.
//

void handleButton() {

    static bool lastReading = HIGH;

    static bool stableState = HIGH;

    static unsigned long lastDebounceTime = 0;

    bool reading =
        digitalRead(BUTTON_PIN);

    // Raw state changed
    if (
        reading != lastReading
    ) {

        lastDebounceTime =
            millis();

        lastReading =
            reading;
    }

    // Wait for stable state
    if (
        millis() - lastDebounceTime >=
        debounceDelay
    ) {

        if (
            reading != stableState
        ) {

            stableState =
                reading;

            // Button pressed
            if (
                stableState == LOW
            ) {

                manualControl = true;

                manualStartTime =
                    millis();

                relayState =
                    !relayState;

                digitalWrite(
                    RELAY_PIN,
                    relayState
                );

                Serial.printf(
                    "GPIO18 -> MANUAL %s | Pump: %s\n",
                    relayState ? "ON" : "OFF",
                    relayState ? "ON" : "OFF"
                );

                publishRelayState();
            }
        }
    }
}

// ============================================================
// GPIO5 - AUTO PUMP SWITCH
// ============================================================
//
// One physical press:
//
// ON  -> OFF
// OFF -> ON
//
// This DOES NOT depend on waterMode.
//
// reCamera cannot change autoPump.
//

void handleAutoPump() {

    static bool lastReading = HIGH;

    static bool stableState = HIGH;

    static unsigned long lastDebounceTime = 0;

    bool reading =
        digitalRead(AUTO_BUTTON_PIN);

    // Raw change
    if (
        reading != lastReading
    ) {

        lastDebounceTime =
            millis();

        lastReading =
            reading;
    }

    // Debounce
    if (
        millis() - lastDebounceTime >=
        debounceDelay
    ) {

        // Stable state changed
        if (
            reading != stableState
        ) {

            stableState =
                reading;

            // Button pressed
            if (
                stableState == LOW
            ) {

                autoPump =
                    !autoPump;

                Serial.print(
                    "GPIO5 -> Auto Pump: "
                );

                Serial.println(
                    autoPump
                        ? "ON"
                        : "OFF"
                );

                // If automatic mode is disabled,
                // immediately stop the pump.
                if (!autoPump) {

                    relayState = false;

                    digitalWrite(
                        RELAY_PIN,
                        LOW
                    );

                    Serial.println(
                        "Auto Pump OFF -> Pump OFF"
                    );

                    publishRelayState();
                }
            }
        }
    }
}

// ============================================================
// GPIO17 - WIFI RESET
// ============================================================
//
// LOW for 3 seconds = reset Wi-Fi.
//
// INPUT_PULLUP means:
//
// HIGH = released
// LOW  = pressed
//

void handleWiFiReset() {

    static int lastReading = HIGH;

    static unsigned long lowStart = 0;

    static bool resetTriggered = false;

    int reading =
        digitalRead(WIFI_RESET_PIN);

    // --------------------------------------------------------
    // Detect GPIO transition
    // --------------------------------------------------------

    if (
        reading != lastReading
    ) {

        Serial.printf(
            "[GPIO17] %s at %lu ms\n",
            reading == LOW
                ? "LOW"
                : "HIGH",
            millis()
        );

        if (reading == LOW) {

            lowStart =
                millis();

            resetTriggered =
                false;
        }

        else {

            if (lowStart != 0) {

                Serial.printf(
                    "[GPIO17] LOW duration: %lu ms\n",
                    millis() - lowStart
                );
            }

            lowStart = 0;

            resetTriggered =
                false;
        }

        lastReading =
            reading;
    }

    // --------------------------------------------------------
    // Long press
    // --------------------------------------------------------

    if (
        reading == LOW &&
        lowStart != 0 &&
        !resetTriggered &&
        millis() - lowStart >=
            wifiResetHoldTime
    ) {

        resetTriggered =
            true;

        Serial.println(
            "[GPIO17] 3 seconds -> RESET Wi-Fi"
        );

        WiFiManager wm;

        wm.resetSettings();

        delay(500);

        ESP.restart();
    }
}

// ============================================================
// PUBLISH RELAY STATE
// ============================================================

void publishRelayState() {

    if (!client.connected()) {
        return;
    }

    client.publish(
        topic_relay,
        relayState
            ? "1"
            : "0"
    );
}

// ============================================================
// SENSOR PUBLISH
// ============================================================

void sendSensorData(
    float t,
    float h,
    float m
) {

    if (
        isnan(t) ||
        isnan(h)
    ) {

        return;
    }

    String payload =
        "{\"temp\":";

    payload +=
        String(t, 2);

    payload +=
        ",\"humi\":";

    payload +=
        String(h, 2);

    payload +=
        ",\"soil\":";

    payload +=
        String(m, 2);

    payload +=
        "}";

    if (client.connected()) {

        client.publish(
            "db/write",
            payload.c_str()
        );
    }
}

// ============================================================
// OLED
// ============================================================

void updateDisplay() {

    display.clearDisplay();

    display.setCursor(0, 0);

    display.setTextSize(1);

    display.setTextColor(WHITE);

    // --------------------------------------------------------
    // Stage
    // --------------------------------------------------------

    display.print("Stage: ");

    if (plant_stage == 1) {

        display.println(
            "VEGETATIVE"
        );

    }
    else if (plant_stage == 2) {

        display.println(
            "HARVEST"
        );

    }
    else {

        display.println(
            "NONE"
        );
    }

    // --------------------------------------------------------
    // Water mode
    // --------------------------------------------------------

    display.print("Mode: ");

    display.println(
        waterMode
    );

    // --------------------------------------------------------
    // Harvest alert
    // --------------------------------------------------------

    if (harvestAlert) {

        display.setTextColor(
            BLACK,
            WHITE
        );

        display.println(
            ">>> HARVEST <<<"
        );

        display.setTextColor(
            WHITE
        );
    }

    // --------------------------------------------------------
    // Sensors
    // --------------------------------------------------------

    display.printf(
        "T:%.1f H:%.1f\n",
        temperature,
        humidity
    );

    display.printf(
        "Soil: %.1f%%\n",
        moisture
    );

    display.printf(
        "Pump: %s\n",
        relayState
            ? "ON"
            : "OFF"
    );

    display.printf(
        "Auto: %s\n",
        autoPump
            ? "ON"
            : "OFF"
    );

    display.display();
}

// ============================================================
// HEARTBEAT LED
// ============================================================

void heartbeatLED() {

    if (
        millis() - previousLedMillis >=
        ledInterval
    ) {

        previousLedMillis =
            millis();

        for (
            int i = 0;
            i < ledCount;
            i++
        ) {

            digitalWrite(
                ledPins[i],
                LOW
            );
        }

        digitalWrite(
            ledPins[ledIndex],
            HIGH
        );

        ledIndex =
            (ledIndex + 1) %
            ledCount;
    }
}