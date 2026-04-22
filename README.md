Real-Time Bluetooth Tracking System

BluStick is a full-stack embedded system that enables real-time tracking of nearby Bluetooth devices using a custom ESP32-based hardware device, a mobile application, and a cloud backend.

The system is designed to provide situational awareness by detecting Bluetooth signals, estimating distance, and visualizing movement patterns on a live map.

---

Key Features

Real-time BLE scanning using ESP32 firmware
Mobile app (React Native) for live detection and visualization
Map-based tracking with route reconstruction
Secure backend with authentication and data storage
Search Mode for tracking specific devices in real time
High-volume data handling (50k+ detections tested)

---

System Architecture

BluStick is built as a complete hardware + software pipeline:

1. ESP32 Device (Firmware)

   * Scans nearby BLE devices
   * Estimates distance using RSSI
   * Streams data over BLE

2. Mobile Application

   * Connects via BLE
   * Processes and visualizes detections
   * Adds GPS data for mapping

3. Backend (Cloud Run + PostgreSQL)

   * Stores detections securely
   * Supports multi-device coordination
   * Handles authentication (JWT)

---

Tech Stack

Hardware / Firmware

* ESP32 (C / ESP-IDF)
* BLE (GAP + GATT)
* RSSI-based distance estimation

Frontend

React Native (Expo)
Google Maps / react-native-maps

Backend

Node.js (Express)
PostgreSQL (Cloud SQL)
Google Cloud Run

---

How It Works

* The ESP32 continuously scans for Bluetooth signals

* Each detection includes:

  * MAC address
  * Signal strength (RSSI)
  * Estimated distance

* The mobile app:

  * Receives detections via BLE
  * Adds GPS coordinates
  * Displays real-time movement on a map

* The backend:

  * Stores detection data
  * Enables multi-device coordination
  * Maintains secure access

---

## 📊 Results

* ✅ ~24 hour battery life
* ✅ ~10% average distance error
* ✅ ~24 meter detection range
* ✅ Stable BLE connection during movement
* ✅ Successfully processed 100k+ detections
