# SmartFarm IoT Automation System

## Overview
SmartFarm is an end-to-end IoT solution for automated plant monitoring and irrigation control. The system integrates computer vision (reCamera), environmental sensors (DHT22, soil moisture), and edge computing (ESP32) to optimize water usage based on plant growth stages detected by AI models.

## Hardware Components
- **ESP32-WROOM**: Main controller (Wi-Fi/MQTT, relay control, sensor interface)
- **ESP32-S3-CAM**: Computer vision node (Edge Impulse model, BLE/Wi-Fi)
- **reCamera (SG2002 + ESP32-C3)**: On-device AI inference (TPU-MLIR models, Node-RED)
- **Sensors**: DHT22 (temp/humidity), Soil moisture (analog), OLED display
- **Actuators**: Relay module (12V pump), Status LEDs, Manual button

## Software Stack
### Firmware
- **ESP32-WROOM**: Arduino core (WiFiManager, PubSubClient, ArduinoJson)
- **ESP32-S3-CAM**: Edge Impulse FOMO (object detection), BLE peripheral
- **reCamera**: Node-RED (SSCMA Model node), TPU-MLIR (ONNX→cvimodel)

### Cloud
- **MQTT Broker**: `mqtt.lunka.io:8883` (TLS)
- **Topics**:
  - `db/write` (sensor data: `{"temp":X,"humi":Y,"soil":Z}`)
  - `cmd/digital` (remote control: `{"pin":19,"value":1}`)
  - `smartfarm/plant_stage` (reCamera detection: `1-4`)
  - `smartfarm/water_mode` (irrigation policy: `"light"|"increase"|"maintain"|"harvest"`)
  - `smartfarm/harvest_alert` (disable pump: `true|false`)

### AI Pipeline
1. **Training**: YOLO11n (lettuce growth stages: Seedling, Vegetative, Harvest)
2. **Conversion**: ONNX → MLIR → INT8 calibration → cvimodel (TPU-MLIR)
3. **Deployment**: reCamera Node-RED (SSCMA Model node)
4. **Integration**: ESP32 subscribes to MQTT topics for irrigation logic

## Key Features
- **reCamera Sync**: Adjusts irrigation thresholds based on plant growth stage
- **Auto-Pump**: Hysteresis control (soil moisture thresholds: 30-70% default)
- **Manual Override**: Button with 60s timeout
- **Watchdog**: ESP32 hardware watchdog (30s timeout)
- **OLED Display**: Real-time sensor data + system status
- **Wi-Fi Provisioning**: WiFiManager portal (`SmartFarm-Setup`)

## Current Status
- **ESP32 Firmware**: `smartfarm_DHT_relay_M.ino` (DHT22, soil, relay, MQTT, OLED)
- **reCamera Model**: `lettuce_int8.cvimodel` (6-output, 640x640, INT8)
- **Node-RED Flow**: Lettuce growth stage detection → MQTT publish

## Development Logs
Detailed daily progress is documented in `/development-logs/` (format: `DD-MM-YYYY-T:T-Topic.md`).

## Setup
1. **Hardware**: Connect sensors/relay to ESP32 (see `config.h` for pinout)
2. **Firmware**: Compile with Arduino IDE (libraries: WiFiManager, PubSubClient, ArduinoJson, Adafruit SSD1306)
3. **reCamera**: Flash `lettuce_int8.cvimodel` via Node-RED web UI (`http://10.3.2.114/#/workspace`)
4. **MQTT**: Configure credentials via WiFiManager portal (`SmartFarm-Setup`)

## License
MIT