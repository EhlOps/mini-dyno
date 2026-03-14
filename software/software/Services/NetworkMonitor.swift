//
//  NetworkMonitor.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import Foundation

@Observable
class NetworkMonitor {
    var isConnectedToDevice = false
    var lastCheckTime: Date?
    var connectionError: String?
    
    private var checkTask: Task<Void, Never>?
    var deviceURL: String
    
    init(deviceURL: String = "http://192.168.4.1/ping") {
        self.deviceURL = deviceURL
    }
    
    func startMonitoring() {
        checkTask?.cancel()
        checkTask = Task {
            while !Task.isCancelled {
                await checkConnection()
                // Once connected, stop polling. If connection is lost mid-session
                // the WebSocket disconnect will surface the error instead.
                if isConnectedToDevice { break }
                try? await Task.sleep(for: .seconds(2))
            }
        }
    }
    
    func stopMonitoring() {
        checkTask?.cancel()
        checkTask = nil
    }
    
    private func checkConnection() async {
        guard let url = URL(string: deviceURL) else {
            connectionError = "Invalid URL"
            isConnectedToDevice = false
            return
        }
        
        var request = URLRequest(url: url)
        request.timeoutInterval = 1.5 // Quick timeout for local network
        
        do {
            let (_, response) = try await URLSession.shared.data(for: request)
            if let httpResponse = response as? HTTPURLResponse,
               (200...299).contains(httpResponse.statusCode) {
                isConnectedToDevice = true
                lastCheckTime = Date()
                connectionError = nil
                return
            }
        } catch {
            connectionError = error.localizedDescription
        }
        
        isConnectedToDevice = false
    }
    
    func checkOnce() async {
        await checkConnection()
    }
}
