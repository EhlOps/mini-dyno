//
//  SavedSessionsView.swift
//  software
//
//  Created by Sam Ehlers on 3/14/26.
//

import SwiftUI

struct SavedSessionsView: View {
    @Bindable var viewModel: DynoViewModel
    @State private var selectedSession: DynoSession?
    @State private var sessionToExport: DynoSession?

    var body: some View {
        List {
            if viewModel.savedSessions.isEmpty {
                ContentUnavailableView(
                    "No Saved Sessions",
                    systemImage: "tray",
                    description: Text("Completed recording sessions will appear here")
                )
            } else {
                ForEach(viewModel.savedSessions) { session in
                    SessionRow(session: session)
                        .onTapGesture {
                            selectedSession = session
                        }
                        .swipeActions(edge: .trailing, allowsFullSwipe: true) {
                            Button(role: .destructive) {
                                viewModel.deleteSession(session)
                            } label: {
                                Label("Delete", systemImage: "trash")
                            }
                        }
                        .swipeActions(edge: .leading) {
                            Button {
                                sessionToExport = session
                            } label: {
                                Label("Export", systemImage: "square.and.arrow.up")
                            }
                            .tint(.blue)
                        }
                }
            }
        }
        .navigationTitle("Session History")
        .sheet(item: $selectedSession) { session in
            SessionDetailView(session: session)
        }
        .sheet(item: $sessionToExport) { session in
            ExportSheetView(session: session)
        }
    }
}

struct SessionRow: View {
    let session: DynoSession
    
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(session.startTime ?? Date(), style: .date)
                    .font(.headline)
                
                Text(session.startTime ?? Date(), style: .time)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            
            HStack(spacing: 16) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Peak Torque")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("\(session.peakTorque, specifier: "%.1f") Nm")
                        .font(.title3)
                        .fontWeight(.semibold)
                        .foregroundStyle(.red)
                }

                VStack(alignment: .leading, spacing: 2) {
                    Text("Peak Power")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("\(session.peakPower, specifier: "%.1f") hp")
                        .font(.title3)
                        .fontWeight(.semibold)
                        .foregroundStyle(.blue)
                }

                Spacer()

                VStack(alignment: .trailing, spacing: 2) {
                    Text("\(session.dataPoints.count)")
                        .font(.title3)
                        .fontWeight(.semibold)
                    Text("points")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(.vertical, 4)
    }
}

struct SessionDetailView: View {
    let session: DynoSession
    @Environment(\.dismiss) private var dismiss
    @State private var exportURLs: [URL] = []

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 20) {
                    GroupBox("Session Information") {
                        VStack(alignment: .leading, spacing: 8) {
                            InfoRow(label: "Date", value: session.startTime?.formatted(date: .long, time: .shortened) ?? "Unknown")
                            if let duration = session.duration {
                                InfoRow(label: "Duration", value: String(format: "%.1f seconds", duration))
                            }
                            InfoRow(label: "Data Points", value: "\(session.dataPoints.count)")
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .padding(.horizontal)

                    DynoGraphView(dataPoints: session.dataPoints)
                        .frame(height: 400)
                        .padding(.horizontal)

                    StatsView(session: session)
                        .padding(.horizontal)

                    ShareLink(items: exportURLs) {
                        Label("Export Data & Graph", systemImage: "square.and.arrow.up")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(exportURLs.isEmpty)
                    .padding(.horizontal)
                }
                .padding(.vertical)
            }
            .navigationTitle("Session Details")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
            .task {
                exportURLs = DataExporter.exportAll(session: session)
            }
        }
    }
}

struct ExportSheetView: View {
    let session: DynoSession
    @Environment(\.dismiss) private var dismiss
    @State private var exportURLs: [URL] = []

    var body: some View {
        NavigationStack {
            VStack(spacing: 24) {
                if exportURLs.isEmpty {
                    ProgressView("Preparing export…")
                } else {
                    ShareLink(items: exportURLs) {
                        Label("Share CSV + Graph", systemImage: "square.and.arrow.up")
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                }
            }
            .padding()
            .navigationTitle("Export Session")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
            .task {
                exportURLs = DataExporter.exportAll(session: session)
            }
        }
        .presentationDetents([.height(180)])
    }
}

struct InfoRow: View {
    let label: String
    let value: String
    
    var body: some View {
        HStack {
            Text(label)
                .foregroundStyle(.secondary)
            Spacer()
            Text(value)
                .fontWeight(.medium)
        }
    }
}

#Preview("Empty") {
    NavigationStack {
        SavedSessionsView(viewModel: DynoViewModel())
    }
}

#Preview("With Sessions") {
    let viewModel = DynoViewModel()
    viewModel.generateTestData()
    viewModel.savedSessions.append(viewModel.currentSession)

    return NavigationStack {
        SavedSessionsView(viewModel: viewModel)
    }
}
