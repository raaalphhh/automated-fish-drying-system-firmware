# 🐟 Automated Fish Drying System – ESP32 Firmware

This repository contains the **firmware** for the *Automated Fish Drying System*, a microcontroller-based drying solution that intelligently controls **temperature**, **airflow**, and **drying duration** to ensure efficient and consistent fish dehydration.  

The system automatically manages **heat guns, fans, and lamps** using an **ESP32**, while sending real-time data to **Firebase** for monitoring through a mobile application.

---

## ⚙️ System Overview

The firmware runs an **automated drying cycle** based on time and temperature thresholds, ensuring precise heat management without user intervention.  
The system alternates between **normal** and **high-temperature** drying phases, automatically maintaining stable environmental conditions.

### 🔑 Control Parameters
| Parameter | Description |
|------------|-------------|
| 🔥 **Heat Guns** | Activate when temperature ≤ 55°C; deactivate above 65°C |
| 💡 **Lamps** | Assist drying, toggled based on temperature |
| 🌬️ **Fan** | Turns ON for cooling when temperature ≥ 55°C |
| 🔄 **Rotor Motor** | Ensures airflow circulation throughout drying |
| ⏱️ **Duration** | Automatic 3-hour drying cycle per session |
| 📦 **Weight Monitoring** | Continuous feedback via HX711 load cell |
| 🌡️ **Temperature Sensing** | Real-time monitoring using DS18B20 sensor |

---

## 🧰 Hardware & Components

| Component | Description |
|------------|-------------|
| **ESP32** | Main controller (WiFi + Firebase integration) |
| **DS18B20** | Digital temperature sensor |
| **HX711** | 24-bit ADC module for weight sensing |
| **Relay Module (8-channel)** | Controls heat guns, fans, and lamps |
| **OLED Display (optional)** | For local monitoring |
| **Firebase** | Cloud database for data logging |
| **Button** | Starts drying session/tare function |

---

## 🧩 Algorithm Summary

Drying follows a **temperature–time-based algorithm**, not weight loss–based.  
This ensures **thermal consistency** and **controlled moisture removal** regardless of environmental fluctuations.

### 🧠 Simplified Process
1. **Preheat Phase** – Heats to ~55°C for 3.5 minutes.  
2. **Normal Drying** – Maintains 55°C with periodic heat gun control.  
3. **High-Temp Phase** – Every 10 minutes, system boosts to 65°C briefly.  
4. **Cooling Control** – Fan engages automatically to prevent overheating.  
5. **Auto Shutdown** – Session ends after 3 hours or manually via the button.  
6. **Data Logging** – Temperature, weight, and duration uploaded to Firebase.  

> This control logic provides consistent drying efficiency and reduces human intervention, suitable for small-scale fishery applications.

---

## 📡 Firebase Data Structure

/fish_drying_system/
├── esp32_ip
├── session_active
├── high_temp
└── fish_drying_sessions/
├── session_19-10-2025_14-32-00/
│ ├── temperature
│ ├── initial_weight
│ ├── final_weight
│ ├── moisture_loss
│ ├── duration
│ ├── timestamp
│ └── epoch_time


---

## 🚀 Getting Started

### 1️⃣ Configure Wi-Fi & Firebase
Update your credentials in the firmware:
```cpp
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define FIREBASE_HOST "https://your-project-id.firebaseio.com/"
#define FIREBASE_AUTH "YOUR_FIREBASE_AUTH_KEY"

2️⃣ Upload to ESP32
Use Arduino IDE or PlatformIO.
Select board: ESP32 Dev Module → Upload.

3️⃣ Monitor Data
Serial Monitor (115200 baud)

Firebase console → /fish_drying_sessions

🧪 Example Output

Temperature: 64.9°C
Weight: 5 kg
Mode: High Temp
Fan: ON | Lamps: OFF | Heatguns: OFF
Session Duration: 3:00:00

📜 License
This firmware is open-source and intended for academic and research use under the MIT License.

👨‍💻 Developer: Ralph Buenaventura
🎓 BS Computer Engineering – Philippines
📍 GitHub: @raaalphhh

