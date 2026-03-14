//
//  SettingsView.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import SwiftUI

struct SettingsView: View {
    @Bindable var viewModel: DynoViewModel
    @Environment(\.dismiss) private var dismiss
    
    @State private var websocketURL: String
    @State private var devicePingURL: String
    
    init(viewModel: DynoViewModel) {
        self.viewModel = viewModel
        _websocketURL = State(initialValue: viewModel.websocketURL)
        _devicePingURL = State(initialValue: viewModel.devicePingURL)
    }
    
    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("WebSocket URL", text: $websocketURL)
                        .textContentType(.URL)
                        .autocorrectionDisabled()
                        #if os(iOS)
                        .textInputAutocapitalization(.never)
                        .keyboardType(.URL)
                        #endif
                    
                    TextField("Device Ping URL", text: $devicePingURL)
                        .textContentType(.URL)
                        .autocorrectionDisabled()
                        #if os(iOS)
                        .textInputAutocapitalization(.never)
                        .keyboardType(.URL)
                        #endif
                } header: {
                    Text("Connection Settings")
                } footer: {
                    Text("Enter the WebSocket URL for data streaming and the HTTP URL for connection testing.")
                }
                
                Section {
                    Button("Reset to Defaults") {
                        websocketURL = AppConfiguration.defaultWebSocketURL
                        devicePingURL = AppConfiguration.defaultDevicePingURL
                    }
                }
                
                Section {
                    HStack {
                        Text("Connection Status")
                        Spacer()
                        if viewModel.networkMonitor.isConnectedToDevice {
                            Label("Connected", systemImage: "checkmark.circle.fill")
                                .foregroundStyle(.green)
                        } else {
                            Label("Disconnected", systemImage: "xmark.circle.fill")
                                .foregroundStyle(.red)
                        }
                    }
                    
                    Button("Test Connection Now") {
                        Task {
                            await viewModel.networkMonitor.checkOnce()
                        }
                    }
                } header: {
                    Text("Status")
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") {
                        dismiss()
                    }
                }
                
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        viewModel.websocketURL = websocketURL
                        viewModel.devicePingURL = devicePingURL
                        viewModel.networkMonitor.deviceURL = devicePingURL
                        dismiss()
                    }
                    .disabled(websocketURL.isEmpty || devicePingURL.isEmpty)
                }
            }
        }
    }
}

#Preview {
    SettingsView(viewModel: DynoViewModel())
}
