# Dyno Monitor — iOS App

iOS companion app for the mini-dyno dynamometer. Connects to the ESP32 over WiFi, streams live torque and RPM readings, plots a traditional dyno graph (torque + power vs RPM) in real time, and exports session data as CSV or PNG.

## Requirements

- iOS 16+
- Xcode 15+
- mini-dyno ESP32 device running the firmware

## How It Works

The ESP32 runs a WiFi access point (`mini-dyno`, open network) and a WebSocket server. The app connects to `ws://192.168.4.1/ws` and receives JSON frames at 50 Hz:

```json
{"torque": 12.45, "rpm": 3200}
```

| Field | Type | Description |
|---|---|---|
| `torque` | float | Torque in Newton-metres |
| `rpm` | float | Engine speed in RPM |

Power is computed in the app: `P (hp) = torque (Nm) × rpm × 2π / 60 / 745.7`

The app plots both torque (Nm, red) and power (hp, blue) against RPM on the same chart, matching a traditional dyno sheet.

## Getting Started

1. Flash the ESP32 firmware (see `../firmware/`)
2. On your iPhone/iPad, go to **Settings → Wi-Fi** and join the `mini-dyno` network
3. Open the app — the connection indicator turns green when the device is reachable
4. Tap **Start** to begin recording, **Stop** to end the session

## App Structure

```
software/
├── Models/
│   ├── DynoDataPoint.swift     — single sample: rpm, torqueNm, computed powerHp
│   └── DynoSession.swift       — recording session; tracks peak torque, peak power, RPM at peaks
├── Services/
│   ├── WebSocketService.swift  — URLSession WebSocket client; parses {"torque","rpm"} frames
│   ├── NetworkMonitor.swift    — polls GET /ping every 2 s to track connectivity
│   └── DataExporter.swift      — exports CSV and graph PNG to the temp directory
├── View Models/
│   └── DynoViewModel.swift     — app state; bridges WebSocket → session data points
├── Utilities/
│   └── AppConfiguration.swift  — network URLs, timing constants, feature flags
└── Views/
    ├── ContentView.swift        — adaptive layout: graph + controls
    ├── ConnectionStatusView.swift
    ├── DynoGraphView.swift      — SwiftUI Charts: torque (red) + power (blue) vs RPM
    ├── StatsView.swift          — peak torque, peak power (with RPM subtitles), point count
    ├── DetailedStatsView.swift  — full run report with export button
    ├── SavedSessionsView.swift  — history list; swipe to delete or export
    └── SettingsView.swift       — edit WebSocket/ping URLs; reset to defaults
```

## UI Overview

The main screen uses an adaptive two-column layout in landscape:

- **Left** — live connection status badge + dyno graph
- **Right** — Start / Stop / Clear / Stats buttons

The toolbar provides access to **Settings** (gear icon, top-left) and **Session History** (clock icon, top-right).

### Settings

| Setting | Default |
|---|---|
| WebSocket URL | `ws://192.168.4.1/ws` |
| Device Ping URL | `http://192.168.4.1/ping` |

### Session History

Completed sessions are listed in reverse chronological order showing peak torque, peak power, and point count. Swipe left to **Export** (CSV + graph PNG) or swipe right to **Delete**.

## Export Format

**CSV** — one row per sample, plus a summary block:

```
Timestamp,RPM,Torque (Nm),Power (hp)
12:34:56.001,1200,42.30,9.65
12:34:56.021,1260,44.80,10.72
...

Summary
Peak Torque (Nm),82.0
Peak Power (hp),38.42
RPM at Peak Torque,3500
RPM at Peak Power,4800
```

**Graph PNG** — 1200×800 px at 2× scale (2400×1600 effective).

## Debug Build

In debug builds a **Test Data** button appears on the main screen. It generates a simulated dyno pull from 1000 to 6000 RPM with a Gaussian torque curve peaking at 85 Nm near 3500 RPM, so the UI can be exercised without hardware.
