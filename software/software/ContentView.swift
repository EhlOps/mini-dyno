//
//  ContentView.swift
//  software
//
//  Created by Sam Ehlers on 3/13/26.
//

import SwiftUI

struct ContentView: View {
    @State private var viewModel = DynoViewModel()
    @State private var showingSettings = false
    @State private var showingDetailedStats = false

    var body: some View {
        NavigationStack {
            GeometryReader { geo in
                let isLandscape = geo.size.width > geo.size.height

                if isLandscape {
                    HStack(spacing: 12) {
                        graphPanel
                        controlPanel(fixedWidth: 220)
                    }
                    .padding(10)
                } else {
                    VStack(spacing: 10) {
                        graphPanel
                        controlPanel(fixedWidth: nil)
                    }
                    .padding(10)
                }
            }
            .navigationTitle("Dyno Monitor")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button(action: { showingSettings = true }) {
                        Label("Settings", systemImage: "gearshape.fill")
                    }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    NavigationLink(destination: SavedSessionsView(viewModel: viewModel)) {
                        Label("History", systemImage: "clock.arrow.circlepath")
                    }
                }
            }
        }
        .onAppear { viewModel.networkMonitor.startMonitoring() }
        .onDisappear { viewModel.networkMonitor.stopMonitoring() }
        .sheet(isPresented: $showingSettings) {
            SettingsView(viewModel: viewModel)
        }
        .sheet(isPresented: $showingDetailedStats) {
            DetailedStatsView(session: viewModel.currentSession)
        }
    }

    private var graphPanel: some View {
        VStack(spacing: 10) {
            ConnectionStatusView(
                isConnected: viewModel.networkMonitor.isConnectedToDevice,
                lastCheckTime: viewModel.networkMonitor.lastCheckTime,
                errorMessage: viewModel.networkMonitor.connectionError
            )
            DynoGraphView(dataPoints: viewModel.currentSession.dataPoints)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
        }
    }

    @ViewBuilder
    private func controlPanel(fixedWidth: CGFloat?) -> some View {
        let buttons = VStack(spacing: 10) {
            Spacer()

            Button(action: { viewModel.startRecording() }) {
                Label("Start", systemImage: "play.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(.green)
            .disabled(!viewModel.networkMonitor.isConnectedToDevice || viewModel.currentSession.isRecording)
            .controlSize(.large)

            Button(action: { viewModel.stopRecording() }) {
                Label("Stop", systemImage: "stop.circle.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(.red)
            .disabled(!viewModel.currentSession.isRecording)
            .controlSize(.large)

            Button(action: { viewModel.clearCurrentSession() }) {
                Label("Clear", systemImage: "trash")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(viewModel.currentSession.isRecording || viewModel.currentSession.dataPoints.isEmpty)
            .controlSize(.large)

            Button(action: { showingDetailedStats = true }) {
                Label("Stats", systemImage: "chart.bar.fill")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .disabled(viewModel.currentSession.dataPoints.isEmpty)
            .controlSize(.large)

            #if DEBUG
            Button(action: { viewModel.generateTestData() }) {
                Label("Test Data", systemImage: "waveform.path.ecg")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .tint(.orange)
            .controlSize(.large)
            #endif

            Spacer()
        }

        if let width = fixedWidth {
            buttons.frame(width: width)
        } else {
            buttons
        }
    }
}

#Preview {
    ContentView()
}
