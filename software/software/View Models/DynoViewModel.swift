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

        // Set up the callback: firmware sends weightGrams and deviceTimestampMs
        webSocketService.onDataReceived = { [weak self] weightGrams, _ in
            self?.addDataPoint(forceGrams: weightGrams)
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

    func addDataPoint(forceGrams: Double) {
        guard currentSession.isRecording else { return }

        let now = Date()
        let elapsed = currentSession.startTime.map { now.timeIntervalSince($0) } ?? 0

        let point = DynoDataPoint(
            timestamp: now,
            elapsedSeconds: elapsed,
            forceGrams: forceGrams
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

        // Simulate a 10-second load cell pull with a peak around 5 s
        for i in 0...50 {
            let t = Double(i) * 0.2
            let peak = 5.0
            let maxForce = 5000.0
            let width = 8.0
            let force = maxForce * exp(-pow(t - peak, 2) / width)

            let point = DynoDataPoint(
                timestamp: (currentSession.startTime ?? Date()).addingTimeInterval(t),
                elapsedSeconds: t,
                forceGrams: max(force, 100)
            )
            currentSession.dataPoints.append(point)
        }

        currentSession.isRecording = false
        currentSession.endTime = (currentSession.startTime ?? Date()).addingTimeInterval(10)
        statusMessage = "Test data generated"
    }
}
