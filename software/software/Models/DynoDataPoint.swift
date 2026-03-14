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
    let elapsedSeconds: Double  // seconds from session start
    let forceGrams: Double       // load cell reading in grams

    init(id: UUID = UUID(), timestamp: Date = Date(), elapsedSeconds: Double, forceGrams: Double) {
        self.id = id
        self.timestamp = timestamp
        self.elapsedSeconds = elapsedSeconds
        self.forceGrams = forceGrams
    }
}
