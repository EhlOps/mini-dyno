# Mini Dyno

### Overview

The mini dyno (dynamometer) is a tool created to measure the torque and power output of an engine or motor. The mechanical portion of the dyno functions as a water brake. This design is suited for engines up to 4 horsepower and is mainly used to measure the output of small engines used in RC cars.

An ESP32-C3 microcontroller reads a load cell (via HX711 ADC) and a Hall effect RPM sensor, converts the readings to torque (Nm) and RPM, then streams them over WiFi to an iOS companion app. The app plots a traditional dyno graph — torque and power vs RPM — in real time.

---

## System Architecture

```
Engine ──── Water Brake ──── Load Cell ──── HX711 ────┐
                  │                                   ESP32-C3 ── WiFi AP ── iPhone
              Hall Sensor (4 magnets/rev) ─────────────┘
```

| Measurement | Sensor | Pin |
|---|---|---|
| Force (→ Torque) | HX711 load cell ADC | PD_SCK: GPIO 7, DOUT: GPIO 6 |
| RPM | Digital Hall effect | GPIO 10 |
| Ready indicator | LED | GPIO 5 |

**Torque arm:** 100 mm (load cell to shaft centre)
**Torque (Nm):** `Force (N) × 0.1 m`
**Power (hp):** `Torque (Nm) × RPM × 2π / 60 / 745.7`

---

## Repository Structure

```
mini-dyno/
├── firmware/   — ESP-IDF project for ESP32-C3
└── software/   — iOS companion app (Xcode / SwiftUI)
```

See the README in each subdirectory for build and usage instructions.
