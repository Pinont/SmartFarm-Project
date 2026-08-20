# SmartFarm IoT Automation System

## Overview
SmartFarm is an end-to-end IoT solution for automated plant monitoring and irrigation control. The system integrates computer vision (reCamera), environmental sensors (DHT22, soil moisture), and edge computing (ESP32) to optimize water usage based on plant growth stages detected by AI models.

# Guide Book
How to download dataset ndjson: https://docs.ultralytics.com/datasets/detect#ultralytics-ndjson-format

How to convert model from yolo11n to cvimodel int8: 
- https://wiki.seeedstudio.com/recamera_model_export_online/ (recommended)
- https://wiki.seeedstudio.com/model_conversion_guide/
- https://wiki.seeedstudio.com/recamera_model_conversion/

## Hardware Components
- **ESP32-WROOM**: Main controller (Wi-Fi/MQTT, relay control, sensor interface)
- **reCamera**: On-device AI inference (TPU-MLIR models, Node-RED)
- **Sensors**: DHT22 (temp/humidity), Soil moisture (analog), OLED display
- **Actuators**: Relay module (12V pump), Status LEDs, Manual button

## Software Stack
### Firmware
- **ESP32-WROOM**: Arduino core (WiFiManager, PubSubClient, ArduinoJson)
- **reCamera**: Node-RED (SSCMA Model node), TPU-MLIR (ONNX→cvimodel)

### Cloud
- **MQTT Broker**: `mqtt.lunka.io:8883`
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
- **Wi-Fi Provisioning**: WiFiManager portal (`AP_IOT_Setup`)

## Current Status
- **ESP32 Firmware**: `smartfarm_DHT_relay_M.ino` (DHT22, soil, relay, MQTT, OLED)
- **reCamera Model**: `lettuce_int8.cvimodel` (6-output, 640x640, INT8)
- **Node-RED Flow**: Lettuce growth stage detection → MQTT publish

## Setup
1. **Hardware**: Connect sensors/relay to ESP32
3. **reCamera**: Flash `lettuce_int8.cvimodel` via Node-RED web UI (`http://192.168.42.1/#/workspace`)
4. **MQTT**: Configure credentials via WiFiManager portal (`AP_IOT_Setup`)