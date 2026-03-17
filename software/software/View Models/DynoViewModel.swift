//
//  DynoViewModel.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import Foundation

@Observable
class DynoViewModel {
    var currentSession = DynoSession()
    var networkMonitor: NetworkMonitor
    var webSocketService = WebSocketService()

    var savedSessions: [DynoSession] = []
    var statusMessage: String = "Ready"

    // Configuration
    var websocketURL: String
    var devicePingURL: String

    init(websocketURL: String = "ws://192.168.4.1/ws", devicePingURL: String = "http://192.168.4.1/ping") {
        self.websocketURL = websocketURL
        self.devicePingURL = devicePingURL
        self.networkMonitor = NetworkMonitor(deviceURL: devicePingURL)

        // Firmware sends torqueNm and rpm
        webSocketService.onDataReceived = { [weak self] torqueNm, rpm in
            self?.addDataPoint(torqueNm: torqueNm, rpm: rpm)
        }
    }

    func startRecording() {
        guard networkMonitor.isConnectedToDevice else {
            statusMessage = "Not connected to dyno device"
            return
        }

        currentSession = DynoSession()
        currentSession.isRecording = true
        currentSession.startTime = Date()

        webSocketService.connect(to: websocketURL)
        statusMessage = "Recording..."
    }

    func stopRecording() {
        currentSession.isRecording = false
        currentSession.endTime = Date()

        webSocketService.disconnect()

        if !currentSession.dataPoints.isEmpty {
            savedSessions.append(currentSession)
            statusMessage = "Session saved with \(currentSession.dataPoints.count) data points"
        } else {
            statusMessage = "No data recorded"
        }
    }

    func addDataPoint(torqueNm: Double, rpm: Double) {
        guard currentSession.isRecording else { return }

        let point = DynoDataPoint(
            timestamp: Date(),
            rpm: rpm,
            torqueNm: torqueNm
        )
        currentSession.dataPoints.append(point)
    }

    func clearCurrentSession() {
        currentSession = DynoSession()
        statusMessage = "Session cleared"
    }

    func deleteSession(_ session: DynoSession) {
        savedSessions.removeAll { $0.id == session.id }
    }

    // For testing/simulation purposes
    func generateTestData() {
        currentSession = DynoSession()
        currentSession.startTime = Date()
        currentSession.isRecording = true

        // Simulate a dyno pull from 1000 to 6000 RPM.
        // Torque peaks around 3500 RPM with a Gaussian curve shape.
        for i in 0...50 {
            let rpm       = 1000.0 + Double(i) * 100.0   // 1000 → 6000 RPM
            let peakRPM   = 3500.0
            let maxTorque = 85.0                          // Nm
            let torque    = maxTorque * exp(-pow(rpm - peakRPM, 2) / 3_000_000.0)

            let point = DynoDataPoint(
                timestamp: (currentSession.startTime ?? Date()).addingTimeInterval(Double(i) * 0.2),
                rpm: rpm,
                torqueNm: max(torque, 5.0)
            )
            currentSession.dataPoints.append(point)
        }

        currentSession.isRecording = false
        currentSession.endTime = (currentSession.startTime ?? Date()).addingTimeInterval(10)
        statusMessage = "Test data generated"
    }
}
