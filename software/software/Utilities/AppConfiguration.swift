//
//  AppConfiguration.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import Foundation

/// Configuration settings for the Dyno Monitor app
struct AppConfiguration {
    
    // MARK: - Network Settings
    
    /// Default WebSocket URL for dyno data streaming
    static let defaultWebSocketURL = "ws://192.168.4.1/ws"
    
    /// Default HTTP ping URL for connection testing
    static let defaultDevicePingURL = "http://192.168.4.1/ping"
    
    /// Network polling interval in seconds
    static let networkPollingInterval: TimeInterval = 2.0
    
    /// WebSocket ping interval to keep connection alive
    static let websocketPingInterval: TimeInterval = 30.0
    
    /// HTTP request timeout for ping checks
    static let pingTimeout: TimeInterval = 1.5
    
    // MARK: - Data Settings

    /// Data smoothing window size (for moving average)
    static let smoothingWindowSize: Int = 3
    
    // MARK: - UI Settings
    
    /// Graph colors
    struct GraphColors {
        static let torque = "red"
        static let horsepower = "blue"
        static let background = "ultraThinMaterial"
    }
    
    /// Chart dimensions for export
    struct ExportDimensions {
        static let width: CGFloat = 1200
        static let height: CGFloat = 800
        static let scale: CGFloat = 2.0  // For retina displays
    }
    
    /// Number formatting
    struct Formatting {
        static let decimalPlaces = 1
        static let dateFormat = "yyyy-MM-dd_HH-mm-ss"
        static let timeFormat = "HH:mm:ss.SSS"
    }
    
    // MARK: - Feature Flags
    
    #if DEBUG
    /// Enable test data generation button
    static let enableTestDataButton = true
    
    /// Enable debug logging
    static let enableDebugLogging = true
    #else
    static let enableTestDataButton = false
    static let enableDebugLogging = false
    #endif
    
    /// Enable haptic feedback (iOS only)
    static let enableHapticFeedback = true
    
    /// Enable session auto-save
    static let enableAutoSave = true
    
    /// Maximum number of saved sessions (0 = unlimited)
    static let maxSavedSessions = 0
    
    // MARK: - Test Data Settings

    struct TestData {
        static let durationSeconds: Double = 10.0
        static let sampleInterval: Double = 0.2
        static let minRPM: Double = 1000
        static let maxRPM: Double = 6000
        static let peakTorqueNm: Double = 85.0
        static let minTorqueNm: Double = 5.0
        static let peakRPM: Double = 3500.0
    }


    // MARK: - File Settings
    
    struct Export {
        static let csvPrefix = "dyno_session_"
        static let graphPrefix = "dyno_graph_"
        static let fileExtensionCSV = "csv"
        static let fileExtensionPNG = "png"
    }
    
    // MARK: - Validation

    /// Validates a torque reading in Newton-metres
    static func isValidTorque(_ nm: Double) -> Bool {
        return nm >= 0 && nm <= 10_000
    }

    /// Validates an RPM reading
    static func isValidRPM(_ rpm: Double) -> Bool {
        return rpm >= 0 && rpm <= 100_000
    }

    // MARK: - User Defaults Keys
    
    struct UserDefaultsKeys {
        static let websocketURL = "dyno.websocket.url"
        static let devicePingURL = "dyno.device.ping.url"
        static let savedSessions = "dyno.saved.sessions"
        static let lastConnectionCheck = "dyno.last.connection.check"
        static let enableHaptics = "dyno.enable.haptics"
        static let preferredUnits = "dyno.preferred.units"  // "imperial" or "metric"
    }
    
}

// MARK: - Debug Helpers

#if DEBUG
extension AppConfiguration {
    /// Prints all configuration values (for debugging)
    static func printConfiguration() {
        print("=== Dyno App Configuration ===")
        print("WebSocket URL: \(defaultWebSocketURL)")
        print("Ping URL: \(defaultDevicePingURL)")
        print("Polling Interval: \(networkPollingInterval)s")

        print("Test Data Enabled: \(enableTestDataButton)")
        print("Debug Logging: \(enableDebugLogging)")
        print("============================")
    }
}
#endif

// MARK: - Usage Examples

/*

 // In NetworkMonitor:
 try? await Task.sleep(for: .seconds(AppConfiguration.networkPollingInterval))

 // In DynoDataPoint:
 var powerHp: Double {
     (torqueNm * rpm * 2.0 * .pi / 60.0) / 745.7
 }

 // In DynoViewModel:
 if AppConfiguration.isValidTorque(point.torqueNm) && AppConfiguration.isValidRPM(point.rpm) {
     currentSession.dataPoints.append(point)
 }

 // In ExportService:
 let url = path.appendingPathComponent("\(AppConfiguration.Export.csvPrefix)\(date).\(AppConfiguration.Export.fileExtensionCSV)")

 // In ContentView:
 #if DEBUG
 if AppConfiguration.enableTestDataButton {
     Button("Generate Test Data") { ... }
 }
 #endif

 */
