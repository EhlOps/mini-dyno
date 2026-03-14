//
//  StatsView.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import SwiftUI

struct StatsView: View {
    let session: DynoSession

    var body: some View {
        GeometryReader { geometry in
            let useVerticalLayout = geometry.size.width < 300

            if useVerticalLayout {
                VStack(spacing: 0) {
                    StatCard(
                        title: "Peak Force",
                        value: String(format: "%.0f", session.peakForce),
                        unit: "g",
                        subtitle: String(format: "@ %.1f s", session.timeAtPeakForce),
                        color: .red
                    )

                    Divider()
                        .padding(.horizontal, 8)

                    StatCard(
                        title: "Avg Force",
                        value: String(format: "%.0f", session.averageForce),
                        unit: "g",
                        subtitle: "",
                        color: .orange
                    )

                    Divider()
                        .padding(.horizontal, 8)

                    StatCard(
                        title: "Data Points",
                        value: "\(session.dataPoints.count)",
                        unit: "",
                        subtitle: session.isRecording ? "Recording..." : "Completed",
                        color: .green
                    )
                }
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
            } else {
                HStack(spacing: 0) {
                    StatCard(
                        title: "Peak Force",
                        value: String(format: "%.0f", session.peakForce),
                        unit: "g",
                        subtitle: String(format: "@ %.1f s", session.timeAtPeakForce),
                        color: .red
                    )

                    Divider()
                        .padding(.vertical, 8)

                    StatCard(
                        title: "Avg Force",
                        value: String(format: "%.0f", session.averageForce),
                        unit: "g",
                        subtitle: "",
                        color: .orange
                    )

                    Divider()
                        .padding(.vertical, 8)

                    StatCard(
                        title: "Data Points",
                        value: "\(session.dataPoints.count)",
                        unit: "",
                        subtitle: session.isRecording ? "Recording..." : "Completed",
                        color: .green
                    )
                }
                .frame(height: 100)
                .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 12))
            }
        }
        .frame(minHeight: 100)
    }
}

struct StatCard: View {
    let title: String
    let value: String
    let unit: String
    let subtitle: String
    let color: Color

    var body: some View {
        VStack(spacing: 4) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)

            HStack(alignment: .firstTextBaseline, spacing: 2) {
                Text(value)
                    .font(.title)
                    .fontWeight(.bold)
                    .foregroundStyle(color)

                if !unit.isEmpty {
                    Text(unit)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Text(subtitle)
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 8)
    }
}

#Preview {
    let session = DynoSession()
    session.dataPoints = [
        DynoDataPoint(elapsedSeconds: 1.0, forceGrams: 1200),
        DynoDataPoint(elapsedSeconds: 2.0, forceGrams: 3800),
        DynoDataPoint(elapsedSeconds: 3.0, forceGrams: 4900),
    ]

    return StatsView(session: session)
        .padding()
}
