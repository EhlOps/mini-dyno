//
//  DynoDataPoint.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import Foundation

struct DynoDataPoint: Identifiable, Codable {
    let id: UUID
    let timestamp: Date
    let rpm: Double         // engine speed in RPM
    let torqueNm: Double    // torque in Newton-metres

    var powerHp: Double {
        // P (W) = τ (Nm) × ω (rad/s) = τ × (2π × RPM / 60)
        // P (hp) = P (W) / 745.7
        (torqueNm * rpm * 2.0 * .pi / 60.0) / 745.7
    }

    init(id: UUID = UUID(), timestamp: Date = Date(), rpm: Double, torqueNm: Double) {
        self.id = id
        self.timestamp = timestamp
        self.rpm = rpm
        self.torqueNm = torqueNm
    }
}
