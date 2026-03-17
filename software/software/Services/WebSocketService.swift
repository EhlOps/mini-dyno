//
//  WebSocketService.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import Foundation

@Observable
class WebSocketService {
    var isConnected = false
    var lastError: String?
    
    private var webSocketTask: URLSessionWebSocketTask?
    private let urlSession = URLSession.shared
    
    // Callback for when data is received
    var onDataReceived: ((Double, Double) -> Void)?
    
    func connect(to urlString: String) {
        guard let url = URL(string: urlString) else {
            lastError = "Invalid WebSocket URL"
            return
        }
        
        disconnect() // Clean up any existing connection
        
        webSocketTask = urlSession.webSocketTask(with: url)
        webSocketTask?.resume()
        isConnected = true
        lastError = nil
        
        receiveMessage()
        
        // Send a ping periodically to keep connection alive
        startPingTimer()
    }
    
    func disconnect() {
        webSocketTask?.cancel(with: .goingAway, reason: nil)
        webSocketTask = nil
        isConnected = false
    }
    
    private func receiveMessage() {
        webSocketTask?.receive { [weak self] result in
            guard let self = self else { return }
            
            switch result {
            case .success(let message):
                self.handleMessage(message)
                // Continue listening
                if self.isConnected {
                    self.receiveMessage()
                }
                
            case .failure(let error):
                self.lastError = error.localizedDescription
                self.isConnected = false
            }
        }
    }
    
    private func handleMessage(_ message: URLSessionWebSocketTask.Message) {
        switch message {
        case .string(let text):
            if let data = text.data(using: .utf8) {
                parseData(data)
            }
        case .data(let data):
            parseData(data)
        @unknown default:
            break
        }
    }
    
    private func parseData(_ data: Data) {
        // Expected JSON format from firmware: {"torque":<Nm>, "rpm":<RPM>}
        do {
            if let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
               let torqueNm = json["torque"] as? Double,
               let rpm = json["rpm"] as? Double {

                onDataReceived?(torqueNm, rpm)
            }
        } catch {
            lastError = "Failed to parse data: \(error.localizedDescription)"
        }
    }
    
    private func startPingTimer() {
        Task {
            while isConnected {
                try? await Task.sleep(for: .seconds(30))
                try? await webSocketTask?.sendPing()
            }
        }
    }
    
    func send(message: String) async throws {
        guard isConnected else {
            throw NSError(domain: "WebSocketService", code: -1, userInfo: [NSLocalizedDescriptionKey: "Not connected"])
        }
        
        let message = URLSessionWebSocketTask.Message.string(message)
        try await webSocketTask?.send(message)
    }
}

// Extension for sendPing
extension URLSessionWebSocketTask {
    func sendPing() async throws {
        return try await withCheckedThrowingContinuation { continuation in
            self.sendPing { error in
                if let error = error {
                    continuation.resume(throwing: error)
                } else {
                    continuation.resume()
                }
            }
        }
    }
}
