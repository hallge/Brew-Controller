// Brew settings (web: Setup › Brew, saveSettings; board: Settings): the cup workflow, stopping on weight
// (offset learning) and the limits, timeouts and SwitchBot press timing.
import SwiftUI

struct SetupBrewView: View {
    var store: SetupStore
    @State private var draft = SettingsModel()
    @State private var loadedOnce = false

    var body: some View {
        Form {
            SetupMessages(error: store.error, notice: store.notice)
            Section("Cup") {
                Picker("Cup workflow", selection: $draft.cupMode) {
                    Text("Cup on scale before Start").tag(0)
                    Text("Tare, then place cup").tag(1)
                }
                .accessibilityIdentifier("setupCupMode")
                Picker("Cup on the scale", selection: $draft.requireCup) {
                    Text("Required before Start").tag(true)
                    Text("Not checked").tag(false)
                }
                .accessibilityIdentifier("setupRequireCup")
                Picker("After tare", selection: $draft.autoStartAfterTare) {
                    Text("Start automatically").tag(true)
                    Text("Wait for a second tap").tag(false)
                }
                .accessibilityIdentifier("setupAfterTare")
                SetupNumberRow(title: "Minimum cup weight (g)", value: $draft.cupMinWeightG, id: "setupCupMin")
            }
            Section("Stopping on weight") {
                Picker("Stop offset learning", selection: $draft.learningEnabled) {
                    Text("On").tag(true)
                    Text("Off").tag(false)
                }
                .accessibilityIdentifier("setupLearning")
                SetupNumberRow(title: "Learning factor (0.01 to 1.00)", value: $draft.learningFactor, id: "setupLearningFactor")
                Picker("Learning factor mode", selection: $draft.adaptiveLearning) {
                    Text("Fixed: always the value entered").tag(false)
                    Text("Adaptive: tuned per controller entry").tag(true)
                }
                .accessibilityIdentifier("setupAdaptive")
                Picker("Drive the scale's timer", selection: $draft.scaleTimer) {
                    Text("Yes").tag(true)
                    Text("No").tag(false)
                }
                .accessibilityIdentifier("setupScaleTimer")
            }
            Section {
                SetupNumberRow(title: "Minimum valid weight (g)", value: $draft.minValidWeightG, id: "setupMinValid")
                SetupIntRow(title: "Scale timeout (ms)", value: $draft.scaleTimeoutMs, id: "setupScaleTimeout")
                SetupIntRow(title: "Post-shot drip observation (ms)", value: $draft.dripObserveMs, id: "setupDrip")
                SetupIntRow(title: "No weight stop before (ms)", value: $draft.minShotTimeMs, id: "setupNoStopBefore")
                SetupIntRow(title: "No-flow timeout (s)", value: $draft.noFlowTimeoutS, id: "setupNoFlow")
                Picker("Press style", selection: $draft.actuationMode) {
                    Text("Bot press (push and return)").tag(0)
                    Text("Controller-timed down / up").tag(1)
                }
                .accessibilityIdentifier("setupPressStyle")
                SetupIntRow(title: "Press duration (ms, down / up style)", value: $draft.pressDurationMs, id: "setupPressDuration")
                SetupIntRow(title: "Release settle time (ms)", value: $draft.releaseSettleMs, id: "setupReleaseSettle")
                SetupIntRow(title: "Bot hold time (s, Bot press style)", value: $draft.botHoldSeconds, id: "setupBotHold")
            } header: {
                Text("Limits, timeouts and SwitchBot press timing (rarely changed)")
            }
            Section {
                Button("Save") { Task { await save() } }
                    .accessibilityIdentifier("setupBrewSave")
            }
        }
        .navigationTitle("Brew")
        .task {
            if !store.loaded { await store.refresh() }
            if !loadedOnce {
                draft = store.settings
                loadedOnce = true
            }
        }
    }

    private func save() async {
        await store.save([
            "cupMode": .int(draft.cupMode),
            "requireCup": .bool(draft.requireCup),
            "autoStartAfterTare": .bool(draft.autoStartAfterTare),
            "cupMinWeightG": .double(draft.cupMinWeightG),
            "learningEnabled": .bool(draft.learningEnabled),
            "learningFactor": .double(draft.learningFactor),
            "adaptiveLearning": .bool(draft.adaptiveLearning),
            "scaleTimer": .bool(draft.scaleTimer),
            "minValidWeightG": .double(draft.minValidWeightG),
            "scaleTimeoutMs": .int(draft.scaleTimeoutMs),
            "dripObserveMs": .int(draft.dripObserveMs),
            "minShotTimeMs": .int(draft.minShotTimeMs),
            "noFlowTimeoutS": .int(draft.noFlowTimeoutS),
            "actuationMode": .int(draft.actuationMode),
            "pressDurationMs": .int(draft.pressDurationMs),
            "releaseSettleMs": .int(draft.releaseSettleMs),
            "botHoldSeconds": .int(draft.botHoldSeconds),
        ])
    }
}

/// A title and a decimal field.
struct SetupNumberRow: View {
    var title: String
    @Binding var value: Double
    var id: String

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            TextField("0", value: $value, format: .number.precision(.fractionLength(0...2)))
                .keyboardType(.decimalPad)
                .multilineTextAlignment(.trailing)
                .frame(width: 90)
                .accessibilityIdentifier(id)
        }
    }
}

/// A title and a whole-number field.
struct SetupIntRow: View {
    var title: String
    @Binding var value: Int
    var id: String

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            TextField("0", value: $value, format: .number.grouping(.never))
                .keyboardType(.numberPad)
                .multilineTextAlignment(.trailing)
                .frame(width: 90)
                .accessibilityIdentifier(id)
        }
    }
}

/// An error in red and a notice in green, at the top of a form.
struct SetupMessages: View {
    var error: String?
    var notice: String?

    var body: some View {
        if let error {
            Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("setupError") }
        } else if let notice {
            Section { Text(notice).foregroundStyle(.green).accessibilityIdentifier("setupNotice") }
        }
    }
}
