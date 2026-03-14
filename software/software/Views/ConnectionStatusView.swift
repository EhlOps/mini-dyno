//
//  ConnectionStatusView.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import SwiftUI

struct ConnectionStatusView: View {
    let isConnected: Bool
    var lastCheckTime: Date?
    var errorMessage: String?
    
    var body: some View {
        HStack(spacing: 8) {
            // Status indicator
            Circle()
                .fill(isConnected ? .green : .red)
                .frame(width: 10, height: 10)
                .shadow(color: isConnected ? .green.opacity(0.5) : .red.opacity(0.5), radius: 4)
            
            // Status text
            VStack(alignment: .leading, spacing: 2) {
                Text(isConnected ? "Connected to Dyno" : "Not Connected")
                    .font(.caption)
                    .fontWeight(.medium)
                
                if let lastCheck = lastCheckTime, isConnected {
                    Text("Last check: \(lastCheck, style: .relative) ago")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                } else if let error = errorMessage, !isConnected {
                    Text(error)
                        .font(.caption2)
                        .foregroundStyle(.red)
                        .lineLimit(1)
                }
            }
            
            Spacer()
            
            // WebSocket indicator
            if isConnected {
                HStack(spacing: 4) {
                    Image(systemName: "wave.3.right")
                        .font(.caption)
                        .foregroundStyle(.green)
                    Text("Ready")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 10))
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .strokeBorder(isConnected ? .green.opacity(0.3) : .red.opacity(0.3), lineWidth: 1)
        )
    }
}

#Preview("Connected") {
    ConnectionStatusView(
        isConnected: true,
        lastCheckTime: Date().addingTimeInterval(-5)
    )
    .padding()
}

#Preview("Disconnected") {
    ConnectionStatusView(
        isConnected: false,
        errorMessage: "Network unreachable"
    )
    .padding()
}
