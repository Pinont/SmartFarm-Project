// SmartFarm ESP32 Configuration
// Pin definitions, MQTT topics, and thresholds

#ifndef CONFIG_H
#define CONFIG_H

// ================= Pin Definitions =================
#define DHT_PIN         32
#define SOIL_PIN        33
#define RELAY_PIN       19
#define BUTTON_PIN      18
#define WIFI_RESET_PIN  17
#define LED_WIFI        14
#define LED_MQTT        12
#define LED_RELAY       13

// ================= MQTT Configuration =================
#define MQTT_HOST       "mqtt.lunka.io"
#define MQTT_PORT       8883
#define MQTT_CLIENT_ID  "ba12512b-6575-44f1-8b09-fd26eea66817"

// Topics
#define TOPIC_SENSORS       "db/write"
#define TOPIC_COMMAND       "cmd/digital"
#define TOPIC_RELAY_STATE   "ba12512b-6575-44f1-8b09-fd26eea66817/pin/19/state"

// ================= Timing =================
#define MANUAL_TIMEOUT_MS   60000   // 60 seconds
#define PUBLISH_INTERVAL_MS 30000   // 30 seconds
#define DHT_INTERVAL_MS     2000    // 2 seconds
#define MQTT_RECONNECT_MS   5000    // 5 seconds
#define WIFI_RECONNECT_MS   30000   // 30 seconds

// ================= Soil Moisture =================
#define SOIL_DRY_VALUE      4095    // ADC value for dry soil
#define SOIL_WET_VALUE      500     // ADC value for wet soil

// ================= Display =================
#define OLED_ADDRESS        0x3C

#endif // CONFIG_H