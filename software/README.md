# Dyno Monitor — iOS App

iOS companion app for the mini-dyno load cell data acquisition system. Connects to the ESP32 over WiFi, streams live force readings, plots them in real time, and exports session data as CSV or PNG.

## Requirements

- iOS 16+
- Xcode 15+
- mini-dyno ESP32 device running the `wifi_ap` firmware

## How It Works

The ESP32 runs a WiFi access point (`mini-dyno`, open network) and a WebSocket server. The app connects to `ws://192.168.4.1/ws` and receives JSON frames at ~5 Hz:

```json
{"w": 3241.5, "t": 12840}
```

| Field | Type | Description |
|-------|------|-------------|
| `w` | float | Load cell reading in grams |
| `t` | uint32 | Device uptime in milliseconds |

The app plots force (g) on the Y axis against elapsed session time (s) on the X axis.

## Getting Started

1. Flash the ESP32 firmware (see `../firmware/`)
2. On your iPhone/iPad, go to **Settings → Wi-Fi** and join the `mini-dyno` network
3. Open the app — the connection indicator turns green when the device is reachable
4. Tap **Start** to begin recording, **Stop** to end the session

## App Structure

```
software/
├── Models/
│   ├── DynoDataPoint.swift     — single sample: elapsedSeconds + forceGrams
│   └── DynoSession.swift       — a recording session; tracks peak/avg force
├── Services/
│   ├── WebSocketService.swift  — URLSession WebSocket client; parses {"w","t"} frames
│   ├── NetworkMonitor.swift    — polls GET /ping every 2 s to track connectivity
│   └── DataExporter.swift      — exports CSV and graph PNG to the temp directory
├── View Models/
│   └── DynoViewModel.swift     — app state; bridges WebSocket → session data points
├── Utilities/
│   └── AppConfiguration.swift  — network URLs, timing constants, feature flags
└── Views/
    ├── ContentView.swift        — landscape split layout: graph left, controls right
    ├── ConnectionStatusView.swift
    ├── DynoGraphView.swift      — SwiftUI Charts: force vs time with Catmull-Rom curve
    ├── StatsView.swift          — peak force, average force, point count
    ├── DetailedStatsView.swift  — full run report with export button
    ├── SavedSessionsView.swift  — history list; swipe to delete or export
    ├── SettingsView.swift       — edit WebSocket/ping URLs; reset to defaults
    └── ShareSheet.swift         — UIActivityViewController wrapper for share sheet
```

## UI Overview

The main screen is landscape-only with a two-column layout:

- **Left** — live connection status badge + force-vs-time chart
- **Right** — Start / Stop / Clear / Stats buttons

The toolbar provides access to **Settings** (gear icon, top-left) and **Session History** (clock icon, top-right).

### Settings

| Setting | Default |
|---------|---------|
| WebSocket URL | `ws://192.168.4.1/ws` |
| Device Ping URL | `http://192.168.4.1/ping` |

Tap **Reset to Defaults** to restore both URLs. Changes take effect on the next recording.

### Session History

Completed sessions are listed in reverse chronological order. Swipe left to **Export** (CSV + graph PNG) or swipe right to **Delete**.

## Export Format

**CSV** — one row per sample, plus a summary block:

```
Timestamp,Elapsed (s),Force (g)
12:34:56.001,0.20,312.4
12:34:56.201,0.40,891.7
...

Summary
Peak Force (g),4937.2
Avg Force (g),2104.8
Time at Peak Force (s),5.00
```

**Graph PNG** — 1200×800 px at 2× scale (2400×1600 effective).

## Debug Build

In debug builds a **Test Data** button appears on the main screen. It generates a 10-second Gaussian force curve (peak ~5000 g at t = 5 s) so the UI can be exercised without hardware.
