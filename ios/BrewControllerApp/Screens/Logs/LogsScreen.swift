// The Logs screen (web: #logs): what is recorded, the live tail, download and clear.
// Pushed by More inside its NavigationStack, so no NavigationStack of its own.
import SwiftUI
import UIKit

struct LogsScreen: View {
    @State private var store = LogsStore()
    @State private var follow = true
    @State private var filter = ""
    @State private var shareFile: LogsFile?
    @State private var confirmClear = false

    private var shownLines: [String] {
        let lines = store.text.split(separator: "\n", omittingEmptySubsequences: true).map(String.init)
        let wanted = filter.trimmingCharacters(in: .whitespaces)
        let kept = wanted.isEmpty ? lines : lines.filter { $0.localizedCaseInsensitiveContains(wanted) }
        return Array(kept.suffix(1000))
    }

    var body: some View {
        List {
            if let error = store.error {
                Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("logsError") }
            } else if let notice = store.notice {
                Section { Text(notice).foregroundStyle(.green).accessibilityIdentifier("logsNotice") }
            }
            recordedSection
            liveSection
        }
        .navigationTitle("Logs")
        .accessibilityIdentifier("logsScreen")
        .task {
            await store.loadConfig()
            while !Task.isCancelled {
                await store.poll()
                try? await Task.sleep(for: .seconds(1.5))
            }
        }
        .sheet(item: $shareFile) { file in
            LogsShareSheet(url: file.url)
        }
        .confirmationDialog("Delete the stored log?", isPresented: $confirmClear, titleVisibility: .visible) {
            Button("Clear stored log", role: .destructive) { Task { await store.clearStored() } }
            Button("Cancel", role: .cancel) {}
        }
    }

    private var recordedSection: some View {
        Section {
            Picker("Detail", selection: Binding(get: { store.config.level }, set: { value in
                store.config.level = value
                Task { await store.setLevel(value) }
            })) {
                ForEach(0..<4, id: \.self) { Text(LogsLevel.names[$0] ?? "").tag($0) }
            }
            .pickerStyle(.menu)
            .accessibilityIdentifier("logsLevel")
            Toggle("Keep on flash, so it survives a restart", isOn: Binding(get: { store.config.keep }, set: { value in
                store.config.keep = value
                Task { await store.setKeep(value) }
            }))
            .accessibilityIdentifier("logsKeep")
            Button("Download the whole log") {
                Task { if let url = await store.downloadFile() { shareFile = LogsFile(url: url) } }
            }
            .accessibilityIdentifier("logsDownload")
            Button("Clear stored log", role: .destructive) { confirmClear = true }
                .accessibilityIdentifier("logsClear")
        } header: {
            Text("What is recorded")
        } footer: {
            Text("Flash: \(store.config.flashBytes / 1024) of \(store.config.flashLimit / 1024) kB, memory: \(store.config.memoryBytes / 1024) kB. Each start-up: the firmware version and why the controller started. Every ten minutes: free memory. Lines are written to flash only between shots.")
        }
    }

    private var liveSection: some View {
        Section("Live") {
            Toggle("Follow", isOn: $follow).accessibilityIdentifier("logsFollow")
            TextField("filter, e.g. scale or W", text: $filter)
                .textInputAutocapitalization(.never).autocorrectionDisabled()
                .accessibilityIdentifier("logsFilter")
            Button("Clear view") { store.clearView() }
                .buttonStyle(.bordered)
                .accessibilityIdentifier("logsClearView")
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(Array(shownLines.enumerated()), id: \.offset) { index, line in
                            Text(line).font(.system(size: 11, design: .monospaced)).id(index)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                }
                .frame(height: 320)
                .onChange(of: shownLines.count) { _, count in
                    if follow, count > 0 { proxy.scrollTo(count - 1, anchor: .bottom) }
                }
            }
            .accessibilityIdentifier("logsView")
        }
    }
}

/// An Identifiable file URL for `.sheet(item:)`.
struct LogsFile: Identifiable {
    var url: URL
    var id: String { url.path }
}

/// The system share sheet for a file (log, backup, CSV).
struct LogsShareSheet: UIViewControllerRepresentable {
    var url: URL

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }

    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
