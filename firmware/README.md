# Mini Dyno — Firmware

ESP-IDF firmware for the ESP32-C3 that reads a load cell and Hall effect RPM sensor, converts the measurements to torque and RPM, and streams them over a WebSocket to the iOS app.

## Requirements

- ESP-IDF v5.x
- ESP32-C3 target (`idf.py set-target esp32c3`)

## Wiring

| Signal | GPIO |
|---|---|
| HX711 PD_SCK (clock) | 7 |
| HX711 DOUT (data) | 6 |
| Hall effect sensor | 10 |
| Ready LED | 5 |

The load cell is mounted **100 mm** from the water brake shaft centre. The Hall sensor detects **4 magnets** spaced 90° apart on the rotor. The ready LED turns on once WiFi, HX711, and Hall sensor initialisation are all complete and the system is actively streaming.

## Build & Flash

```bash
cd firmware
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSBx flash monitor
```

## Calibration

The `CALIBRATION_SCALE` constant in `main/main.c` converts raw HX711 counts to grams:

1. Flash and let the scale tare (no load at power-on).
2. Place a known weight on the load cell.
3. Run `idf.py monitor` and note the raw value from `hx711_read_average()`.
4. Compute: `scale = (raw − offset) / known_weight_grams`
5. Update `CALIBRATION_SCALE` and reflash.

## WebSocket Output

The device runs a WiFi access point (`mini-dyno`, open network, `192.168.4.1`) and streams JSON frames to `ws://192.168.4.1/ws` at **50 Hz**:

```json
{"torque": 12.45, "rpm": 3200}
```

| Field | Unit | Description |
|---|---|---|
| `torque` | Nm | Torque = Force (N) × 0.1 m arm |
| `rpm` | RPM | Engine speed from Hall sensor |

A `/ping` endpoint (`http://192.168.4.1/ping`) returns `pong` and is used by the iOS app for connection monitoring.

## Component Overview

### `components/hx711`
Thread-safe driver for the HX711 24-bit ADC. Uses a FreeRTOS mutex for task-level safety and a `portMUX` spinlock for bit-bang timing. Key API:

```c
hx711_init(), hx711_tare(), hx711_set_scale(), hx711_get_units()
```

### `components/hall_sensor`
Driver for a digital Hall effect sensor. A rising-edge GPIO interrupt timestamps each magnet pass using `esp_timer_get_time()`. RPM is computed from a rolling window of the last 8 pulse timestamps. Returns `0.0` if no pulse has arrived in the last 500 ms (engine stalled). Key API:

```c
hall_sensor_init(), hall_sensor_get_rpm()
```

### `components/wifi_ap`
WiFi access point + HTTP server with WebSocket support. The broadcast is non-blocking (`httpd_queue_work` pattern). Key API:

```c
wifi_ap_init(), wifi_ap_ws_broadcast()
```

## Project Structure

```
firmware/
├── CMakeLists.txt
├── sdkconfig.defaults          (enables CONFIG_HTTPD_WS_SUPPORT)
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  (app_main, HX711 loop, broadcast task)
└── components/
    ├── hx711/                  (load cell ADC driver)
    ├── hall_sensor/            (Hall effect RPM driver)
    └── wifi_ap/                (WiFi AP + WebSocket server)
```
