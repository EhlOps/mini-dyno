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

    var body: some View {
        VStack(alignment: .leading) {
            Text("Force vs Time")
                .font(.headline)
                .padding(.leading)

            if dataPoints.isEmpty {
                ContentUnavailableView(
                    "No Data",
                    systemImage: "chart.line.uptrend.xyaxis",
                    description: Text("Start recording to see load cell data")
                )
            } else {
                Chart {
                    ForEach(dataPoints) { point in
                        LineMark(
                            x: .value("Time (s)", point.elapsedSeconds),
                            y: .value("Force (g)", point.forceGrams)
                        )
                        .foregroundStyle(.red)
                        .symbol {
                            Circle()
                                .fill(.red)
                                .frame(width: 4, height: 4)
                        }
                        .interpolationMethod(.catmullRom)
                    }
                }
                .chartXAxis {
                    AxisMarks(position: .bottom) { value in
                        AxisGridLine()
                        AxisTick()
                        AxisValueLabel {
                            if let t = value.as(Double.self) {
                                Text(String(format: "%.1f", t))
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
                                Text("\(Int(val))")
                                    .font(.caption)
                            }
                        }
                    }
                }
                .chartXAxisLabel("Time (s)", alignment: .center)
                .chartYAxisLabel("Force (g)", alignment: .leading)
                .padding()
            }
        }
    }
}

#Preview {
    let sampleData: [DynoDataPoint] = (0...50).map { i in
        let t = Double(i) * 0.2
        let peak = 5.0
        let force = 5000.0 * exp(-pow(t - peak, 2) / 8.0)
        return DynoDataPoint(
            timestamp: Date(),
            elapsedSeconds: t,
            forceGrams: max(force, 100)
        )
    }

    return DynoGraphView(dataPoints: sampleData)
        .frame(height: 400)
}
