// "How advice is worked out" (web: the Grinder page's settings card, POST /api/settings).
import SwiftUI

struct GrinderAdviceSettingsView: View {
    var store: GrinderStore
    @State private var draft = GrinderAdviceSettings()
    @State private var guessText = ""
    @State private var maxStepText = ""
    @State private var loadedOnce = false

    var body: some View {
        Form {
            GrinderMessages(error: store.error, notice: store.notice)
            Section {
                Stepper(value: $draft.grindToleranceS, in: 0.5...10, step: 0.5) {
                    Text("Brew time tolerance: ± \(BrewFormat.decimal(draft.grindToleranceS, 1)) s")
                }
                .accessibilityIdentifier("grinderTolerance")
                Stepper(value: $draft.grindWindow, in: 1...10) {
                    Text("Shots averaged: \(draft.grindWindow)")
                }
                .accessibilityIdentifier("grinderWindow")
                Toggle("Refine the advice towards the shots I rated 4 or 5 stars, inside its tolerance", isOn: $draft.grindRatedTarget)
                    .accessibilityIdentifier("grinderRatedTarget")
                Toggle("Size each change by how well past advice landed", isOn: $draft.grindConfidence)
                    .accessibilityIdentifier("grinderConfidence")
                Picker("Room", selection: $draft.grindRoom) {
                    Text("Allow for the room’s temperature once two coffees agree on it (needs Home Assistant)").tag(1)
                    Text("Original rule: allow for it as soon as one coffee shows it").tag(2)
                    Text("Off: times as they ran").tag(0)
                }
                .accessibilityIdentifier("grinderRoom")
                HStack {
                    Text("Starting guess (\(store.dial.unit) per s)")
                    Spacer()
                    TextField("2", text: $guessText).keyboardType(.decimalPad).multilineTextAlignment(.trailing)
                        .frame(width: 80).accessibilityIdentifier("grinderUmPerS")
                }
                HStack {
                    Text("Largest single change (\(store.dial.unit))")
                    Spacer()
                    TextField("10", text: $maxStepText).keyboardType(.numberPad).multilineTextAlignment(.trailing)
                        .frame(width: 80).accessibilityIdentifier("grinderMaxStep")
                }
                Picker("Brew time measured", selection: $draft.grindTimeBasis) {
                    Text("Total: start press to stop (with pre-infusion)").tag(0)
                    Text("Pour: first drops to stop").tag(1)
                }
                .accessibilityIdentifier("grinderTimeBasis")
            } footer: {
                Text("Changing how brew time is measured starts a fresh comparison: set each recipe's target time to match.")
            }
            Section {
                Button("Save") { Task { await save() } }
                    .accessibilityIdentifier("grinderAdviceSave")
            }
        }
        .navigationTitle("How advice is worked out")
        .task {
            if !store.loaded { await store.refresh() }
            if !loadedOnce {
                draft = store.settings
                guessText = BrewFormat.decimal(draft.grindUmPerS, 1)
                maxStepText = String(draft.grindMaxStepUm)
                loadedOnce = true
            }
        }
    }

    private func save() async {
        var body: [String: BrewJSON] = [
            "grindToleranceS": .double(draft.grindToleranceS),
            "grindWindow": .int(draft.grindWindow),
            "grindRatedTarget": .bool(draft.grindRatedTarget),
            "grindConfidence": .bool(draft.grindConfidence),
            "grindTimeBasis": .int(draft.grindTimeBasis),
        ]
        switch draft.grindRoom {
        case 2: body["grindRoom"] = .int(2)
        case 1: body["grindRoom"] = .bool(true)
        default: body["grindRoom"] = .bool(false)
        }
        if let guess = Double(guessText.replacingOccurrences(of: ",", with: ".")), guess >= 0.5, guess <= 20 {
            body["grindUmPerS"] = .double(guess)
        }
        if let step = Int(maxStepText), step >= 1, step <= 100 { body["grindMaxStepUm"] = .int(step) }
        await store.perform("Saved") {
            try await BrewAPI.shared.post("/api/settings", json: body)
        }
    }
}
