# BluStick — Real-Time Bluetooth Tracking System

> Full-stack embedded system for real-time BLE device detection, distance estimation, and live map visualization.

![ESP32](https://img.shields.io/badge/ESP32-C%2FC%2B%2B-blue?style=flat-square)
![React Native](https://img.shields.io/badge/Mobile-React%20Native-61DAFB?style=flat-square)
![Node.js](https://img.shields.io/badge/Backend-Node.js-339933?style=flat-square)
![GCP](https://img.shields.io/badge/Cloud-GCP%20Cloud%20Run-4285F4?style=flat-square)
![PostgreSQL](https://img.shields.io/badge/Database-PostgreSQL-336791?style=flat-square)

---

## Overview

BluStick is a hardware + software pipeline built to provide situational awareness by scanning nearby Bluetooth devices, estimating distance via RSSI, and visualizing movement on a live map. Built as a senior capstone project at Texas A&M University.

---

## Results

| Metric | Value |
|---|---|
| Battery Life | ~24 hours |
| Detection Range | ~24 meters |
| Distance Error | ~10% avg |
| Detections Processed | 100,000+ |
| BLE Stability | Stable during active movement |

---

## System Architecture
ESP32 Firmware  →  React Native App  →  Cloud Backend
(BLE scan)       (map + GPS)          (store + auth)

### ESP32 (Firmware)
- Continuously scans nearby BLE devices
- Estimates distance using RSSI
- Streams detection data over BLE (GAP + GATT)

### Mobile App (React Native)
- Connects to ESP32 via BLE
- Attaches GPS coordinates to each detection
- Visualizes movement on Google Maps (path mode, bubble mode, age-coded coloring)
- Search Mode: target specific devices via ESP32 whitelist

### Backend (Cloud Run + PostgreSQL)
- JWT-authenticated REST API
- Stores detection events with timestamps and coordinates
- Supports multi-device coordination

---

## Tech Stack

**Firmware:** ESP32-WROOM, C / ESP-IDF, BLE GAP + GATT  
**Mobile:** React Native (Expo), Google Maps, react-native-maps, react-native-ble-plx  
**Backend:** Node.js, Express, PostgreSQL (Cloud SQL), Google Cloud Run  

---

## Hardware Design

| Component | Part |
|---|---|
| Microcontroller | ESP32-WROOM (BLE 4.2) |
| Battery | 3.7V Li-Po, 10,000 mAh |
| Charging | TP4056 (USB-C + protection) |
| Voltage Regulation | MT3608 boost converter (3.7V → 5V) |
| Motor Driver | L9110S H-Bridge |
| Feedback | Vibration motor |
| Controls | Push button + slide switch |

---

## Engineering Highlights

- Implemented Douglas-Peucker path simplification for efficient route rendering
- Resolved ESP32 mutex race conditions in BLE firmware
- Fixed NullPointerException crash in BLE cleanup on the mobile layer
- Haversine distance calculations for accurate GPS-based path reconstruction
- Optimized for high-volume data: 100k+ detections tested end-to-end

---

*Capstone · Texas A&M University*
