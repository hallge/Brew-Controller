// Diagnostics (web: Setup › Diagnostics, Report a problem, Relay address): reports on or off, the status,
// the last crash, what would be sent, send now, a problem report and the relay address.
import SwiftUI

struct SetupDiagnosticsView: View {
    var store: SetupStore
    @State private var report = SetupDiagReport()
    @State private var crashText = ""
    @State private var previewText: String?
    @State private var showPreview = false
    @State private var kind = "other"
    @State private var contact = ""
    @State private var message = ""
    @State private var bugState = ""
    @State private var relay = ""
    @State private var loadedOnce = false

    private var statusLine: String {
        guard report.on else { return "Off: nothing is sent." }
        var bits = ["On"]
        bits.append(report.waiting > 0 ? "\(report.waiting) waiting to send" : "nothing waiting")
        if let date = BrewFormat.date(fromEpoch: report.lastSent) {
            bits.append("last sent \(date.formatted(date: .abbreviated, time: .shortened))")
        } else {
            bits.append("none sent yet")
        }
        if !report.lastProblem.isEmpty { bits.append(report.lastProblem) }
        if !report.keyId.isEmpty { bits.append("key id \(report.keyId)") }
        return bits.joined(separator: " · ")
    }

    var body: some View {
        Form {
            SetupMessages(error: store.error, notice: store.notice)
            Section {
                Toggle("Send diagnostics reports", isOn: Binding(
                    get: { store.settings.diagnosticsOn },
                    set: { value in
                        store.settings.diagnosticsOn = value
                        Task {
                            await store.save(["diagnosticsOn": .bool(value)], done: value ? "Diagnostics on" : "Diagnostics off: nothing is sent")
                            await loadReport()
                        }
                    }))
                    .accessibilityIdentifier("setupDiagToggle")
                Text(statusLine).font(.footnote).foregroundStyle(.secondary).accessibilityIdentifier("setupDiagStatus")
                if !crashText.isEmpty {
                    Text("Last crash: \(crashText)").font(.footnote).foregroundStyle(.orange)
                        .accessibilityIdentifier("setupDiagCrash")
                }
                Button("What would be sent") { Task { await preview() } }
                    .accessibilityIdentifier("setupDiagPreview")
                Button("Send now") { Task { await sendNow() } }
                    .disabled(!report.on)
                    .accessibilityIdentifier("setupDiagSend")
            } header: {
                Text("Diagnostics")
            } footer: {
                Text("A short technical report once a day, and after a crash, so problems in other homes get found and fixed. Sent: the firmware version and restarts; which scale, pushers and machine type are in use; how their Bluetooth connections went; how shots and power presses ended; memory; warnings from the log, with names and addresses taken out. Never sent: Bluetooth or IP addresses, passwords, tokens, coffee, roaster or drinker names, notes, subscriptions or where you are. Reports are signed with this controller's own key (its short id is all that identifies it).")
            }
            Section {
                Picker("What it is about", selection: $kind) {
                    ForEach(SetupBugKinds.all, id: \.0) { Text($0.1).tag($0.0) }
                }
                .accessibilityIdentifier("setupBugKind")
                TextField("How to reach you (optional): a GitHub name or an email", text: $contact)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .onChange(of: contact) { _, value in if value.count > 64 { contact = String(value.prefix(64)) } }
                    .accessibilityIdentifier("setupBugContact")
                TextField("What you did, what you expected, and what happened instead.", text: $message, axis: .vertical)
                    .lineLimit(3...8)
                    .onChange(of: message) { _, value in if value.count > 1000 { message = String(value.prefix(1000)) } }
                    .accessibilityIdentifier("setupBugText")
                HStack {
                    Button("Send the report") { Task { await sendBug() } }
                        .accessibilityIdentifier("setupBugSend")
                    Spacer()
                    Text("\(message.count) / 1000").font(.caption).foregroundStyle(.secondary)
                }
                if !bugState.isEmpty { Text(bugState).font(.footnote).accessibilityIdentifier("setupBugState") }
            } header: {
                Text("Report a problem")
            } footer: {
                Text("Something not working? Say what happened: it goes to the maintainer with the log attached. Addresses, network and device names, coffee and drinker names are taken out before it is sent, as for every report. The contact goes as you type it. Needs diagnostics on.")
            }
            Section {
                TextField("blank: the built-in relay", text: $relay)
                    .textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                    .accessibilityIdentifier("setupRelayField")
                Button("Save") { Task { await saveRelay() } }
                    .accessibilityIdentifier("setupRelaySave")
            } header: {
                Text("Relay address")
            } footer: {
                Text("For testing; diagnostics and the shared library both use it. https, or http://127.0.0.1.")
            }
        }
        .navigationTitle("Diagnostics")
        .task {
            if !store.loaded { await store.refresh() }
            await loadReport()
            if let crash = try? await store.api.getRaw("/api/crash") {
                crashText = Self.crashSummary(crash.body)
            }
        }
        .sheet(isPresented: $showPreview) {
            NavigationStack {
                ScrollView {
                    Text(previewText ?? "").font(.system(size: 11, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading).padding()
                        .accessibilityIdentifier("setupDiagPreviewText")
                }
                .navigationTitle("What would be sent")
                .navigationBarTitleDisplayMode(.inline)
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { showPreview = false } } }
            }
        }
    }

    /// One line for the last crash report, "" when there is none (`{}`).
    static func crashSummary(_ body: Data) -> String {
        guard let object = try? JSONSerialization.jsonObject(with: body) as? [String: Any], !object.isEmpty else { return "" }
        let parts = ["reason", "task", "version", "firmware"].compactMap { key in (object[key] as? String).map { "\(key) \($0)" } }
        return parts.isEmpty ? "recorded" : parts.joined(separator: ", ")
    }

    private func loadReport() async {
        if let value = try? await store.api.get("/api/diagnostics/report", as: SetupDiagReport.self) {
            report = value
            if !loadedOnce {
                relay = value.relayDefault ? "" : value.relay
                loadedOnce = true
            }
        }
    }

    private func preview() async {
        do {
            let response = try await store.api.getRaw("/api/diagnostics/preview")
            let text = String(decoding: response.body, as: UTF8.self)
            if let object = try? JSONSerialization.jsonObject(with: response.body),
               let pretty = try? JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys]) {
                previewText = String(decoding: pretty, as: UTF8.self)
            } else {
                previewText = text
            }
            showPreview = true
        } catch let failure as BrewAPIError {
            store.error = failure.message
        } catch {
            store.error = String(describing: error)
        }
    }

    private func sendNow() async {
        await store.perform("Sending a report", refresh: false) {
            try await store.api.post("/api/diagnostics/send")
        }
        try? await Task.sleep(for: .seconds(8))
        await loadReport()
    }

    private func sendBug() async {
        let text = message.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else {
            store.error = "Say what happened first"
            return
        }
        let ok = await store.perform(nil, refresh: false) {
            try await store.api.post("/api/diagnostics/bug", json: [
                "kind": .string(kind), "contact": .string(contact), "text": .string(text),
            ])
        }
        if ok {
            bugState = "Sent. Thank you."
            message = ""
            await loadReport()
        } else {
            bugState = ""
        }
    }

    private func saveRelay() async {
        await store.perform("Saved", refresh: false) {
            try await store.api.post("/api/diagnostics/relay", json: ["url": .string(relay.trimmingCharacters(in: .whitespaces))])
        }
        await loadReport()
    }
}
