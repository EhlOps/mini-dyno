//
//  DataExporter.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import SwiftUI
import Charts

struct DataExporter {

    // Export CSV
    static func exportCSV(session: DynoSession) -> URL? {
        var csvText = "Timestamp,Elapsed (s),Force (g)\n"

        let dateFormatter = DateFormatter()
        dateFormatter.dateFormat = "HH:mm:ss.SSS"

        for point in session.dataPoints {
            let timestamp = dateFormatter.string(from: point.timestamp)
            csvText.append("\(timestamp),\(point.elapsedSeconds),\(point.forceGrams)\n")
        }

        csvText.append("\nSummary\n")
        csvText.append("Peak Force (g),\(session.peakForce)\n")
        csvText.append("Avg Force (g),\(session.averageForce)\n")
        csvText.append("Time at Peak Force (s),\(session.timeAtPeakForce)\n")

        let dateFormatter2 = DateFormatter()
        dateFormatter2.dateFormat = "yyyy-MM-dd_HH-mm-ss"
        let dateString = dateFormatter2.string(from: session.startTime ?? Date())
        let path = FileManager.default.temporaryDirectory
            .appendingPathComponent("dyno_session_\(dateString).csv")

        do {
            try csvText.write(to: path, atomically: true, encoding: .utf8)
            return path
        } catch {
            print("DataExporter: CSV write failed: \(error)")
            return nil
        }
    }

    // Export graph as PNG. Must be called on the main actor.
    @MainActor
    static func exportGraph(session: DynoSession) -> URL? {
        let exportSize = CGSize(width: 1200, height: 800)

        let view = DynoGraphView(dataPoints: session.dataPoints)
            .frame(width: exportSize.width, height: exportSize.height)
            .background(Color.white)

        let renderer = ImageRenderer(content: view)
        renderer.scale = 2.0
        // proposedSize must be set explicitly, otherwise Charts returns nil from uiImage
        renderer.proposedSize = .init(exportSize)

        #if os(iOS)
        guard let image = renderer.uiImage else {
            print("DataExporter: ImageRenderer returned nil")
            return nil
        }
        guard let data = image.pngData() else {
            print("DataExporter: pngData() returned nil")
            return nil
        }
        #elseif os(macOS)
        guard let image = renderer.nsImage else {
            print("DataExporter: ImageRenderer returned nil")
            return nil
        }
        guard let tiffData = image.tiffRepresentation,
              let bitmapImage = NSBitmapImageRep(data: tiffData),
              let data = bitmapImage.representation(using: .png, properties: [:]) else {
            print("DataExporter: PNG conversion failed")
            return nil
        }
        #endif

        let dateFormatter = DateFormatter()
        dateFormatter.dateFormat = "yyyy-MM-dd_HH-mm-ss"
        let dateString = dateFormatter.string(from: session.startTime ?? Date())
        let path = FileManager.default.temporaryDirectory
            .appendingPathComponent("dyno_graph_\(dateString).png")

        do {
            try data.write(to: path)
            return path
        } catch {
            print("DataExporter: PNG write failed: \(error)")
            return nil
        }
    }

    @MainActor
    static func exportAll(session: DynoSession) -> [URL] {
        var urls: [URL] = []
        if let csv = exportCSV(session: session) { urls.append(csv) }
        if let graph = exportGraph(session: session) { urls.append(graph) }
        return urls
    }
}
