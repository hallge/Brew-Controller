// Backup and restore (web: Setup › Backup and restore): a backup to Files through the share sheet, and a
// restore from Files: check the file, show what it holds, apply, then the restart screen.
import SwiftUI
import UniformTypeIdentifiers

struct SetupBackupView: View {
    var store: SetupStore
    @State private var includeHistory = true
    @State private var includeSecrets = false
    @State private var shareFile: LogsFile?
    @State private var picking = false
    @State private var summary: SetupRestoreSummary?
    @State private var summaryName = ""
    @State private var confirmApply = false
    @State private var busy = false
    @State private var restored = false

    var body: some View {
        Form {
            SetupMessages(error: store.error, notice: store.notice)
            Section {
                Toggle("Include the shot history", isOn: $includeHistory)
                    .accessibilityIdentifier("setupBackupHistory")
                Toggle("Include the Wi-Fi and bot passwords, the library and Home Assistant tokens, the MQTT password and the signing key (keep that file private)", isOn: $includeSecrets)
                    .accessibilityIdentifier("setupBackupSecrets")
                Button("Download backup") { Task { await download() } }
                    .disabled(busy)
                    .accessibilityIdentifier("setupBackupDownload")
            } header: {
                Text("Backup")
            } footer: {
                Text("One file with this controller's settings, coffees, recipes, grinder, pairing and shots. Save it to Files or send it with AirDrop.")
            }
            Section {
                Button("Restore from a file…") { picking = true }
                    .disabled(busy)
                    .accessibilityIdentifier("setupRestoreChoose")
                if let summary {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(summaryName).fontWeight(.semibold)
                        Text("Firmware \(summary.firmware.isEmpty ? "unknown" : summary.firmware), \(summary.records) records, \(summary.files) files"
                             + (summary.history ? ", with the shot history" : ", without the shot history")
                             + (summary.secrets ? ", with passwords and the signing key" : ""))
                            .font(.footnote).foregroundStyle(.secondary)
                            .accessibilityIdentifier("setupRestoreSummary")
                    }
                    Button("Restore this file", role: .destructive) { confirmApply = true }
                        .accessibilityIdentifier("setupRestoreApply")
                }
                if restored {
                    Button("Restored, tap to restart") { Task { await AppLifecycle.shared.confirmRestart() } }
                        .buttonStyle(.borderedProminent)
                        .accessibilityIdentifier("setupRestoreRestart")
                }
            } header: {
                Text("Restore")
            } footer: {
                Text("A restore replaces the settings, coffees, recipes, grinder and pairing, then the app restarts. The shot history too, when the backup has it; without it, the shots here stay. A backup without passwords keeps the ones this controller has now. A backup without the signing key keeps this controller's key: if the keys differ, sharing starts over under this one. A backup from a board needs its scale and bots paired again here.")
            }
        }
        .navigationTitle("Backup and restore")
        .sheet(item: $shareFile) { file in LogsShareSheet(url: file.url) }
        .fileImporter(isPresented: $picking, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
            if case .success(let urls) = result, let url = urls.first { Task { await check(url) } }
        }
        .confirmationDialog("Restore this file?", isPresented: $confirmApply, titleVisibility: .visible) {
            Button("Restore and restart", role: .destructive) { Task { await apply() } }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The settings, coffees, recipes, grinder and pairing here are replaced, and the app restarts.")
        }
    }

    private func download() async {
        busy = true
        defer { busy = false }
        store.error = nil
        do {
            let file = try await store.api.download("/api/backup",
                                                    query: ["history": includeHistory ? "1" : "0", "secrets": includeSecrets ? "1" : "0"],
                                                    fallbackName: "brew-controller-backup.bbwbak", timeoutMs: 120_000)
            let url = FileManager.default.temporaryDirectory.appendingPathComponent(file.filename)
            try file.data.write(to: url, options: .atomic)
            shareFile = LogsFile(url: url)
        } catch let failure as BrewAPIError {
            store.error = failure.message
        } catch {
            store.error = String(describing: error)
        }
    }

    private func check(_ url: URL) async {
        busy = true
        defer { busy = false }
        store.error = nil
        summary = nil
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            let data = try Data(contentsOf: url)
            let response = try await store.api.upload("/api/restore", fileData: data, fileName: url.lastPathComponent)
            summary = try BrewAPI.decode(SetupRestoreSummary.self, from: response.body)
            summaryName = url.lastPathComponent
        } catch let failure as BrewAPIError {
            store.error = failure.message
        } catch {
            store.error = "That file could not be read: \(error.localizedDescription)"
        }
    }

    private func apply() async {
        busy = true
        defer { busy = false }
        let ok = await store.perform(nil, refresh: false) {
            try await store.api.post("/api/restore/apply")
        }
        if ok {
            summary = nil
            restored = true
            store.notice = "Restored. The app restarts: tap to restart."
        }
    }
}
