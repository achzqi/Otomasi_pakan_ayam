# Otomasi_pakan_ayam 🐔

**Smart Poultry Feeder** merupakan sistem otomatisasi pemberian pakan ayam petelur berbasis **Internet of Things (IoT)** menggunakan **ESP32** sebagai mikrokontroler utama. Sistem ini dirancang untuk membantu peternak dalam menjaga konsistensi pemberian pakan ayam secara **tepat waktu, terjadwal, dan real-time** guna mendukung produktivitas ayam petelur.

---

## 📌 Background

Ayam petelur merupakan salah satu komoditas utama penyedia protein hewani yang berkontribusi terhadap **ketahanan pangan masyarakat**. Produktivitas ayam petelur dipengaruhi oleh berbagai faktor, salah satunya adalah **manajemen pemberian pakan yang konsisten**.

Pada praktik peternakan skala kecil hingga menengah, pemberian pakan masih dilakukan secara manual sehingga berpotensi mengalami keterlambatan atau ketidakkonsistenan jadwal. Oleh karena itu, dikembangkan sistem **Smart Poultry Feeder** untuk membantu proses pemberian pakan secara otomatis berbasis IoT.

---

## 🎯 Objectives

- Mengotomatisasi pemberian pakan ayam petelur.
- Menjaga konsistensi jadwal feeding ayam.
- Monitoring kondisi kandang secara real-time.
- Mengurangi human error pada proses feeding.
- Mendukung produktivitas ayam petelur.

---

## ⚙️ Features

✅ Automatic Feeding Schedule  
✅ Real-Time Monitoring  
✅ LCD Display Interface  
✅ Keypad Configuration Input  
✅ Load Cell Feed Measurement  
✅ Temperature & Humidity Monitoring  
✅ Air Quality Monitoring (MQ135)  
✅ ESP32 IoT-Based System

---

## 🛠 Hardware Components

| Component | Description |
|---|---|
| ESP32 DevKit V1 | Main Controller |
| LCD I2C | User Interface Display |
| I2C Keypad | Input Configuration |
| DHT11 | Temperature & Humidity Sensor |
| HX711 + Load Cell | Feed Weight Measurement |
| MQ135 | Air Quality Sensor |
| Motor Driver | Feed Motor Controller |
| DC Motor | Feed Dispenser Actuator |

---
<img width="574" height="666" alt="Screenshot 2026-06-04 195647" src="https://github.com/user-attachments/assets/60d3c70e-b8df-4a87-9684-48b69fb7a024" />

# 🔌 ESP32 Wiring Configuration

## GPIO Mapping

| Component | ESP32 GPIO | Description |
|---|---:|---|
| LCD SDA | GPIO21 | I2C Data |
| LCD SCL | GPIO22 | I2C Clock |
| Keypad SDA | GPIO21 | Shared I2C Data |
| Keypad SCL | GPIO22 | Shared I2C Clock |
| DHT11 DATA | GPIO16 | Temperature & Humidity Sensor |
| HX711 DT | GPIO32 | Load Cell Data |
| HX711 SCK | GPIO33 | Load Cell Clock |
| MQ135 AO | GPIO34 | Air Quality Sensor |
| Motor Driver IN1 | GPIO26 | Motor Direction Control |
| Motor Driver IN2 | GPIO27 | Motor Direction Control |

---

## ⚡ Power Configuration

```text
5V  → LCD VCC
     Keypad VCC
     DHT11 VCC
     HX711 VCC
     MQ135 VCC

GND → Shared Ground
```

All components share a common **ground (GND)** to ensure stable communication and accurate sensor readings.

---

## 🔄 System Workflow

```text
START
   ↓
Initialize ESP32 & Sensors
   ↓
Connect to WiFi
   ↓
Read Feeding Schedule
   ↓
Check Current Time
   ↓
Is Feeding Time?
      ├── YES → Activate Motor Feeder
      │           ↓
      │      Measure Feed Weight
      │           ↓
      │      Update LCD
      │
      └── NO → Monitor Sensors
                    ↓
           Read Temperature
           Read Humidity
           Read Air Quality
                    ↓
                LOOP
```

---

## 🧠 System Architecture

### Input
- DHT11 (Temperature & Humidity)
- MQ135 (Air Quality)
- HX711 + Load Cell
- Keypad Input

### Process
- ESP32 Microcontroller

### Output
- LCD Display
- Motor Driver + Motor Feeder

---

## 📷 Project Preview

> Add prototype images here.

Example:

```md
![Prototype](assets/prototype.jpg)
```

---

<img width="1000" height="1000" alt="video demo" src="https://github.com/user-attachments/assets/5871a9d2-5673-4d6d-b260-bd25faf02c63" />
video demo

---

## 🚀 Installation

1. Clone repository

```bash
git clone https://github.com/yourusername/smart-poultry-feeder.git
```

2. Open Arduino IDE

3. Install required libraries:

```text
LiquidCrystal_I2C
HX711
DHT Sensor Library
Wire
Keypad_I2C
WiFi
```

4. Upload code to **ESP32**

---

## 👨‍💻 Contributors

**Kelompok 4 — Tugas Besar Mikrokontroler**

-Achmad Zidan Al Baihaqi
 2309106084

-Muh Ghazy Daffa Sampe
 2309106063

-Ahmad Fauzan Ramadhan
 2309106094

-Aditya Wedakarna
 2309106100
---

## 📚 References

This project was developed based on research related to:

- IoT-Based Smart Livestock Systems  
- Poultry Feeding Management  
- ESP32 Smart Feeder Systems  
- Poultry Environmental Monitoring
