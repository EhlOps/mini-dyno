//
//  DynoSession.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import Foundation

@Observable
class DynoSession: Identifiable, Codable {
    var id: UUID
    var startTime: Date?
    var endTime: Date?
    var dataPoints: [DynoDataPoint]
    var isRecording: Bool

    var peakTorque: Double {
        dataPoints.map { $0.torqueNm }.max() ?? 0
    }

    var peakPower: Double {
        dataPoints.map { $0.powerHp }.max() ?? 0
    }

    var rpmAtPeakTorque: Double {
        dataPoints.max(by: { $0.torqueNm < $1.torqueNm })?.rpm ?? 0
    }

    var rpmAtPeakPower: Double {
        dataPoints.max(by: { $0.powerHp < $1.powerHp })?.rpm ?? 0
    }

    var averageTorque: Double {
        guard !dataPoints.isEmpty else { return 0 }
        return dataPoints.reduce(0.0) { $0 + $1.torqueNm } / Double(dataPoints.count)
    }

    var duration: TimeInterval? {
        guard let start = startTime, let end = endTime else { return nil }
        return end.timeIntervalSince(start)
    }

    init(id: UUID = UUID(), startTime: Date? = nil, endTime: Date? = nil, dataPoints: [DynoDataPoint] = [], isRecording: Bool = false) {
        self.id = id
        self.startTime = startTime
        self.endTime = endTime
        self.dataPoints = dataPoints
        self.isRecording = isRecording
    }

    // Codable conformance for @Observable
    enum CodingKeys: CodingKey {
        case id, startTime, endTime, dataPoints, isRecording
    }

    required init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decode(UUID.self, forKey: .id)
        startTime = try container.decodeIfPresent(Date.self, forKey: .startTime)
        endTime = try container.decodeIfPresent(Date.self, forKey: .endTime)
        dataPoints = try container.decode([DynoDataPoint].self, forKey: .dataPoints)
        isRecording = try container.decode(Bool.self, forKey: .isRecording)
    }

    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(id, forKey: .id)
        try container.encodeIfPresent(startTime, forKey: .startTime)
        try container.encodeIfPresent(endTime, forKey: .endTime)
        try container.encode(dataPoints, forKey: .dataPoints)
        try container.encode(isRecording, forKey: .isRecording)
    }
}
