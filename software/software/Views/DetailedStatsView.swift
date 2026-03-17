//
//  DetailedStatsView.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import SwiftUI
import Charts

struct DetailedStatsView: View {
    let session: DynoSession
    @Environment(\.dismiss) private var dismiss
    @State private var exportURLs: [URL] = []

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 24) {
                    // Header
                    VStack(spacing: 8) {
                        Text("Run Summary")
                            .font(.title2)
                            .fontWeight(.bold)

                        if let startTime = session.startTime {
                            Text(startTime, style: .date)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                            Text(startTime, style: .time)
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .padding(.top)

                    // Peak Performance Section
                    VStack(spacing: 16) {
                        SectionHeader(title: "Peak Performance", icon: "bolt.fill")

                        HStack(spacing: 16) {
                            DetailedStatCard(
                                title: "Peak Torque",
                                value: String(format: "%.1f", session.peakTorque),
                                unit: "Nm",
                                color: .red,
                                icon: "gauge.high"
                            )

                            DetailedStatCard(
                                title: "Peak Power",
                                value: String(format: "%.1f", session.peakPower),
                                unit: "hp",
                                color: .blue,
                                icon: "bolt.circle"
                            )
                        }
                    }

                    // RPM Details Section
                    VStack(spacing: 16) {
                        SectionHeader(title: "RPM Details", icon: "waveform.path.ecg")

                        VStack(spacing: 12) {
                            DetailRow(
                                label: "RPM at Peak Torque",
                                value: String(format: "%.0f RPM", session.rpmAtPeakTorque),
                                icon: "speedometer",
                                color: .red
                            )

                            Divider()

                            DetailRow(
                                label: "RPM at Peak Power",
                                value: String(format: "%.0f RPM", session.rpmAtPeakPower),
                                icon: "speedometer",
                                color: .blue
                            )

                            Divider()

                            DetailRow(
                                label: "Avg Torque",
                                value: String(format: "%.1f Nm", session.averageTorque),
                                icon: "equal.circle",
                                color: .orange
                            )
                        }
                        .padding()
                        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
                    }

                    // Run Information Section
                    VStack(spacing: 16) {
                        SectionHeader(title: "Run Information", icon: "info.circle.fill")

                        VStack(spacing: 12) {
                            DetailRow(
                                label: "Total Data Points",
                                value: "\(session.dataPoints.count)",
                                icon: "chart.xyaxis.line",
                                color: .purple
                            )

                            Divider()

                            DetailRow(
                                label: "Run Duration",
                                value: runDuration,
                                icon: "timer",
                                color: .orange
                            )

                            Divider()

                            DetailRow(
                                label: "Status",
                                value: session.isRecording ? "Recording..." : "Completed",
                                icon: session.isRecording ? "record.circle" : "checkmark.circle",
                                color: session.isRecording ? .red : .green
                            )
                        }
                        .padding()
                        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
                    }

                    // Export Actions
                    ShareLink(items: exportURLs) {
                        Label("Export Full Report", systemImage: "square.and.arrow.up")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(exportURLs.isEmpty)
                    .padding(.bottom)
                }
                .padding(.horizontal)
            }
            .navigationTitle("Detailed Statistics")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
            }
            .task {
                exportURLs = DataExporter.exportAll(session: session)
            }
        }
    }

    // MARK: - Computed Properties

    private var runDuration: String {
        guard !session.dataPoints.isEmpty,
              let firstPoint = session.dataPoints.first,
              let lastPoint = session.dataPoints.last else {
            return "N/A"
        }
        let duration = lastPoint.timestamp.timeIntervalSince(firstPoint.timestamp)
        return String(format: "%.1f sec", duration)
    }
}

// MARK: - Supporting Views

struct SectionHeader: View {
    let title: String
    let icon: String

    var body: some View {
        HStack {
            Image(systemName: icon)
                .foregroundStyle(.blue)
            Text(title)
                .font(.headline)
            Spacer()
        }
    }
}

struct DetailedStatCard: View {
    let title: String
    let value: String
    let unit: String
    let color: Color
    let icon: String

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: icon)
                .font(.title)
                .foregroundStyle(color)

            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)

            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(value)
                    .font(.title2)
                    .fontWeight(.bold)
                    .foregroundStyle(color)

                Text(unit)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .frame(maxWidth: .infinity)
        .padding()
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
    }
}

struct DetailRow: View {
    let label: String
    let value: String
    let icon: String
    let color: Color

    var body: some View {
        HStack {
            Image(systemName: icon)
                .foregroundStyle(color)
                .frame(width: 24)

            Text(label)
                .font(.subheadline)

            Spacer()

            Text(value)
                .font(.subheadline)
                .fontWeight(.semibold)
                .foregroundStyle(color)
        }
    }
}

#Preview {
    let session = DynoSession()
    session.dataPoints = [
        DynoDataPoint(rpm: 2000, torqueNm: 45.0),
        DynoDataPoint(rpm: 3500, torqueNm: 82.0),
        DynoDataPoint(rpm: 5000, torqueNm: 60.0),
    ]

    return DetailedStatsView(session: session)
}
