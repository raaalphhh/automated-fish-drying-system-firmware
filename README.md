# 🐟 Automated Fish Drying System Firmware  
### (ESP32-Based Drying Controller with Firebase Integration)

This repository contains the **firmware code** for the thesis project **“Automated Microcontroller-Based Fish Drying System for Enhanced Drying Performance.”** The firmware runs on an **ESP32** microcontroller that automates heat, fan, and rotor control, monitors real-time **temperature and weight**, and sends data to **Firebase** for live monitoring via a mobile app.

---

## ⚙️ System Overview

The system uses:
- **DS18B20 sensor** – to monitor drying temperature  
- **HX711 load cell** – to measure fish weight and track moisture loss  
- **Relays** – to control heat guns, lamps, rotor, and fan  
- **Firebase** – as a real-time database for logging and monitoring  
- **WebServer** – to handle local HTTP requests from the Flutter app  

Together, these create an automated drying process that alternates between **normal** and **high-temperature cycles**, ensuring consistent drying efficiency.

---

## 🔥 Process Algorithm

Drying is controlled using **temperature and time-based logic**:

1. **Preheat Phase:**  
   - Duration: ~3.5 minutes  
   - Target temperature: 55°C  
   - Heaters and lamps run until preheat temperature is reached.  

2. **Main Drying Phase:**  
   - Duration: 3 hours total  
   - High-temp mode triggers every 10 minutes (up to 65°C).  
   - Normal mode maintains ~55°C.  
   - Fan activates automatically for cooling when overheated.  

3. **Automatic Session End:**  
   - After 3 hours, the system saves data to Firebase:
     - Temperature  
     - Initial & final weight  
     - Moisture loss (%)  
     - Duration and timestamp  

---

## 🧮 Firebase Data Structure

```plaintext
/fish_drying_system/
    ├── esp32_ip
    ├── session_active
    ├── high_temp
    └── fish_drying_sessions/
          ├── session_19-10-2025_14-32-00/
          │     ├── temperature
          │     ├── initial_weight
          │     ├── final_weight
          │     ├── moisture_loss
          │     ├── duration
          │     ├── timestamp
          │     └── epoch_time
```

## ⚡ Hardware Connections

```
| Component       | Pin (ESP32) | Description                  |
| --------------- | ----------- | ---------------------------- |
| HX711 (DT, SCK) | 33, 32      | Load cell interface          |
| DS18B20         | GPIO 4      | Temperature input            |
| Relays          | 13–21       | Heat guns, lamps, fan, rotor |
| Push Button     | GPIO 5      | Start/tare control           |

```

## 🧰 Required Libraries
```
| Library           | Purpose                         |
| ----------------- | ------------------------------- |
| FirebaseESP32     | Firebase database communication |
| HX711             | Load cell weight measurement    |
| DallasTemperature | DS18B20 reading                 |
| Adafruit_SSD1306  | OLED display (optional)         |
| WebServer         | Handles local GET requests      |
| WiFi              | Network connection for Firebase |

```

## 🔗 Integration with Flutter App
```
The ESP32 firmware pushes real-time data to Firebase paths,
which are read and visualized in the companion app:
automated-fish-drying-system-flutterapp
```

### 🧑‍💻 Developer
```
Ralph Buenaventura
🎓 Bachelor of Science in Computer Engineering
📍 Philippines
🔗 GitHub Profile: @raaalphhh
```
