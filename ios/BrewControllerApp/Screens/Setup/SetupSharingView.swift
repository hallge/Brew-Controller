// Shared library (web: Setup › Shared library, saveShare): state, key id, the link, sharing on and what it
// shares, the outbox, "queue coffees not sent yet" and "send everything again".
import SwiftUI

struct SetupSharingView: View {
    var store: SetupStore
    @State private var status = SetupShareStatus()
    @State private var repository = ""
    @State private var token = ""
    @State private var enabled = false
    @State private var coffees = false
    @State private var dialIns = false
    @State private var shots = false
    @State private var ratings = false
    @State private var loadedOnce = false
    @State private var confirmAgain = false

    private var stateWord: String {
        !store.settings.shareEnabled ? "off" : status.canSend ? "sending" : "queueing"
    }

    var body: some View {
        Form {
            SetupMessages(error: store.error, notice: store.notice)
            Section {
                HStack {
                    Text("State")
                    Spacer()
                    Text(stateWord).fontWeight(.semibold)
                        .foregroundStyle(store.settings.shareEnabled ? Color.green : Color.secondary)
                        .accessibilityIdentifier("setupShareState")
                }
                if status.keyReady {
                    Text("This controller's key id: \(status.keyId). "
                         + (status.realSignatures ? "Signatures: ES256. Records travel by \(status.via.isEmpty ? "HTTPS" : status.via)."
                            : "This build has no real key, and marks what it writes as \(status.algorithm). The library will not accept these records; they are for trying the flow."))
                        .font(.footnote)
                        .accessibilityIdentifier("setupShareKey")
                } else {
                    Text("No device key: nothing can be shared.").font(.footnote)
                }
                Text("\(status.waiting) waiting to send (room for \(status.capacity)), \(status.sent) sent.")
                    .font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("setupShareQueue")
                if !status.problem.isEmpty {
                    Text(status.problem).font(.footnote).foregroundStyle(.orange)
                }
                Text(status.viaRelay ? "Sharing goes through the Brew Controller relay: no GitHub account needed." : "Sharing goes to your own library repository.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            Section {
                TextField("Library repository (owner/name)", text: $repository)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("setupShareRepo")
                SecureField(status.tokenSet ? "Token for that repository only (set; blank keeps it)" : "Token for that repository only", text: $token)
                    .accessibilityIdentifier("setupShareToken")
            } header: {
                Text("Use your own library repository (optional)")
            } footer: {
                Text("For running another library, or testing. The token should be a fine-grained one limited to this repository, with Contents: read and write and nothing else. Save below stores it on the controller; it is never shown again. Both filled in, they are used instead of the relay; clear the repository to go back to the relay.")
            }
            Section("Share from this controller") {
                Toggle("Share from this controller", isOn: $enabled).accessibilityIdentifier("setupShareEnabled")
                Toggle("Coffees I add or change (name, roaster, origin, process, roast level, notes)", isOn: $coffees)
                Toggle("Settled dial-ins: grind, dose, yield, brew time, grinder, roast date", isOn: $dialIns)
                Toggle("Every shot that counts for grind advice, with its flow", isOn: $shots)
                Toggle("My taste ratings, with those shots", isOn: $ratings)
            }
            Section {
                Button("Save") { Task { await save() } }
                    .accessibilityIdentifier("setupShareSave")
                Button("Queue coffees not sent yet") { Task { await queue("/api/share/coffees", "coffees queued") } }
                    .accessibilityIdentifier("setupShareQueueCoffees")
                Button("Send everything again") { confirmAgain = true }
                    .accessibilityIdentifier("setupShareAgain")
            } footer: {
                Text("Every record is signed with this controller's own key. It carries the key's short id, never a name, an address or anything about the network. Coffees and grinders wait for a reviewer before they are published; measurements do not.")
            }
        }
        .navigationTitle("Shared library")
        .task { await load() }
        .confirmationDialog("Send everything again?", isPresented: $confirmAgain, titleVisibility: .visible) {
            Button("Send everything again") { Task { await queue("/api/share/again", "queued") } }
            Button("Cancel", role: .cancel) {}
        }
    }

    private func load() async {
        if !store.loaded { await store.refresh() }
        if let value = try? await store.api.get("/api/share", as: SetupShareStatus.self) { status = value }
        guard !loadedOnce else { return }
        loadedOnce = true
        let s = store.settings
        enabled = s.shareEnabled
        coffees = s.shareCoffees
        dialIns = s.shareDialIns
        shots = s.shareShots
        ratings = s.shareRatings
        repository = status.viaRelay ? "" : status.repository
    }

    private func save() async {
        await store.save([
            "shareEnabled": .bool(enabled), "shareCoffees": .bool(coffees), "shareDialIns": .bool(dialIns),
            "shareShots": .bool(shots), "shareRatings": .bool(ratings),
        ], done: nil)
        guard store.error == nil else { return }
        await saveLink()
    }

    private func saveLink() async {
        var body: [String: BrewJSON] = ["repository": .string(repository.trimmingCharacters(in: .whitespaces))]
        if !token.isEmpty { body["token"] = .string(token) }
        let linked = await store.perform("Saved") {
            try await store.api.post("/api/share/link", json: body)
        }
        if linked { token = "" }
        if let value = try? await store.api.get("/api/share", as: SetupShareStatus.self) { status = value }
    }

    private func queue(_ path: String, _ word: String) async {
        var count = 0
        await store.perform(nil, refresh: false) {
            let response = try await store.api.post(path)
            if let object = try? JSONSerialization.jsonObject(with: response.body) as? [String: Any] {
                count = object["queued"] as? Int ?? 0
            }
        }
        if store.error == nil { store.notice = "\(count) \(word)" }
        if let value = try? await store.api.get("/api/share", as: SetupShareStatus.self) { status = value }
    }
}
