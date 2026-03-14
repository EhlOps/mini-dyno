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

                    // Peak Force Section
                    VStack(spacing: 16) {
                        SectionHeader(title: "Peak Performance", icon: "bolt.fill")

                        HStack(spacing: 16) {
                            DetailedStatCard(
                                title: "Peak Force",
                                value: String(format: "%.0f", session.peakForce),
                                unit: "g",
                                color: .red,
                                icon: "gauge.high"
                            )

                            DetailedStatCard(
                                title: "Avg Force",
                                value: String(format: "%.0f", session.averageForce),
                                unit: "g",
                                color: .orange,
                                icon: "equal.circle"
                            )
                        }
                    }

                    // Load Details Section
                    VStack(spacing: 16) {
                        SectionHeader(title: "Load Details", icon: "waveform.path.ecg")

                        VStack(spacing: 12) {
                            DetailRow(
                                label: "Peak Force at",
                                value: String(format: "%.2f s", session.timeAtPeakForce),
                                icon: "speedometer",
                                color: .red
                            )

                            Divider()

                            DetailRow(
                                label: "Time Range",
                                value: timeRange,
                                icon: "arrow.left.and.right",
                                color: .green
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

    private var timeRange: String {
        guard !session.dataPoints.isEmpty else { return "N/A" }
        let first = session.dataPoints.first!.elapsedSeconds
        let last = session.dataPoints.last!.elapsedSeconds
        return String(format: "%.1f – %.1f s", first, last)
    }

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
        DynoDataPoint(elapsedSeconds: 1.0, forceGrams: 1200),
        DynoDataPoint(elapsedSeconds: 2.0, forceGrams: 3800),
        DynoDataPoint(elapsedSeconds: 3.0, forceGrams: 4900),
    ]

    return DetailedStatsView(session: session)
}
