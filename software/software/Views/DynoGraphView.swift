//
//  DynoGraphView.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import SwiftUI
import Charts

struct DynoGraphView: View {
    let dataPoints: [DynoDataPoint]

    private var sortedPoints: [DynoDataPoint] {
        dataPoints.sorted { $0.rpm < $1.rpm }
    }

    var body: some View {
        VStack(alignment: .leading) {
            Text("Torque & Power vs RPM")
                .font(.headline)
                .padding(.leading)

            if dataPoints.isEmpty {
                ContentUnavailableView(
                    "No Data",
                    systemImage: "chart.line.uptrend.xyaxis",
                    description: Text("Start recording to see dyno data")
                )
            } else {
                Chart {
                    ForEach(sortedPoints) { point in
                        LineMark(
                            x: .value("RPM", point.rpm),
                            y: .value("Value", point.torqueNm)
                        )
                        .foregroundStyle(by: .value("Series", "Torque (Nm)"))
                        .interpolationMethod(.catmullRom)
                    }
                    ForEach(sortedPoints) { point in
                        LineMark(
                            x: .value("RPM", point.rpm),
                            y: .value("Value", point.powerHp)
                        )
                        .foregroundStyle(by: .value("Series", "Power (hp)"))
                        .interpolationMethod(.catmullRom)
                    }
                }
                .chartForegroundStyleScale([
                    "Torque (Nm)": Color.red,
                    "Power (hp)":  Color.blue
                ])
                .chartXAxis {
                    AxisMarks(position: .bottom) { value in
                        AxisGridLine()
                        AxisTick()
                        AxisValueLabel {
                            if let rpm = value.as(Double.self) {
                                Text("\(Int(rpm))")
                                    .font(.caption)
                            }
                        }
                    }
                }
                .chartYAxis {
                    AxisMarks(position: .leading) { value in
                        AxisGridLine()
                        AxisTick()
                        AxisValueLabel {
                            if let val = value.as(Double.self) {
                                Text(String(format: "%.0f", val))
                                    .font(.caption)
                            }
                        }
                    }
                }
                .chartXAxisLabel("RPM", alignment: .center)
                .chartYAxisLabel("Torque (Nm) / Power (hp)", alignment: .leading)
                .padding()
            }
        }
    }
}

#Preview {
    let sampleData: [DynoDataPoint] = (0...50).map { i in
        let rpm     = 1000.0 + Double(i) * 100.0
        let torque  = 85.0 * exp(-pow(rpm - 3500.0, 2) / 3_000_000.0)
        return DynoDataPoint(
            timestamp: Date(),
            rpm: rpm,
            torqueNm: max(torque, 5.0)
        )
    }

    return DynoGraphView(dataPoints: sampleData)
        .frame(height: 400)
}
