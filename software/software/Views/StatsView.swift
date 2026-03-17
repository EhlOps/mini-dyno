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
                        title: "Peak Torque",
                        value: String(format: "%.1f", session.peakTorque),
                        unit: "Nm",
                        subtitle: String(format: "@ %.0f RPM", session.rpmAtPeakTorque),
                        color: .red
                    )

                    Divider()
                        .padding(.horizontal, 8)

                    StatCard(
                        title: "Peak Power",
                        value: String(format: "%.1f", session.peakPower),
                        unit: "hp",
                        subtitle: String(format: "@ %.0f RPM", session.rpmAtPeakPower),
                        color: .blue
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
                        title: "Peak Torque",
                        value: String(format: "%.1f", session.peakTorque),
                        unit: "Nm",
                        subtitle: String(format: "@ %.0f RPM", session.rpmAtPeakTorque),
                        color: .red
                    )

                    Divider()
                        .padding(.vertical, 8)

                    StatCard(
                        title: "Peak Power",
                        value: String(format: "%.1f", session.peakPower),
                        unit: "hp",
                        subtitle: String(format: "@ %.0f RPM", session.rpmAtPeakPower),
                        color: .blue
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
        DynoDataPoint(rpm: 2000, torqueNm: 45.0),
        DynoDataPoint(rpm: 3500, torqueNm: 82.0),
        DynoDataPoint(rpm: 5000, torqueNm: 60.0),
    ]

    return StatsView(session: session)
        .padding()
}
