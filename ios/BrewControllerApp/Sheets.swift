import SwiftUI

/// Target weight and dose of the active recipe. Takes effect with the next shot.
struct RecipeSheet: View {
    let session: ShotSession
    @Environment(\.dismiss) private var dismiss
    @State private var target = 36.0
    @State private var dose = 18.0

    var body: some View {
        NavigationStack {
            Form {
                Section("Recipe") {
                    Stepper(value: $target, in: 10...100, step: 0.5) {
                        Text("Target  \(String(format: "%.1f", target)) g")
                    }
                    .accessibilityIdentifier("targetStepper")
                    Stepper(value: $dose, in: 5...30, step: 0.5) {
                        Text("Dose  \(String(format: "%.1f", dose)) g")
                    }
                    .accessibilityIdentifier("doseStepper")
                    Text(String(format: "Ratio 1 : %.2f", dose > 0 ? target / dose : 0))
                        .foregroundStyle(.secondary)
                }
                Section {
                    Text("The stop offset is learned by the core from each shot; it is not set here.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
            }
            .navigationTitle("Recipe")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        session.setRecipe(targetG: target, doseG: dose)
                        dismiss()
                    }
                    .accessibilityIdentifier("doneButton")
                }
                ToolbarItem(placement: .cancellationAction) { Button("Cancel") { dismiss() } }
            }
        }
        .onAppear {
            target = session.recipeTargetG
            dose = session.recipeDoseG
        }
    }
}

/// The simulator's fault switches.
struct FaultsSheet: View {
    let session: ShotSession
    @Environment(\.dismiss) private var dismiss
    @State private var pauseSeconds = 10

    var body: some View {
        NavigationStack {
            Form {
                Section("Next press") {
                    Toggle("Lose the Bot's acknowledgement", isOn: Binding(
                        get: { session.lostAckArmed }, set: { session.setFault(Fault.lostAck, $0) }))
                        .accessibilityIdentifier("lostAckToggle")
                    Toggle("Miss the stop (machine ignores the press)", isOn: Binding(
                        get: { session.missedStopArmed }, set: { session.setFault(Fault.missedStop, $0) }))
                        .accessibilityIdentifier("missedStopToggle")
                }
                Section("Scale") {
                    Toggle("Scale goes silent", isOn: Binding(
                        get: { session.scaleSilent }, set: { session.setFault(Fault.scaleSilent, $0) }))
                        .accessibilityIdentifier("scaleSilentToggle")
                }
                Section {
                    Stepper(value: $pauseSeconds, in: 1...60) { Text("Pause ticks for \(pauseSeconds) s") }
                    Button("Pause now") {
                        session.pauseTicks(seconds: pauseSeconds)
                        dismiss()
                    }
                    .accessibilityIdentifier("pauseNowButton")
                } header: {
                    Text("Suspension")
                } footer: {
                    Text("The machine goes on for this long, but the core gets no ticks and no readings, as if iOS had suspended the app. Then it is ticked with the time that has passed.")
                }
            }
            .navigationTitle("Faults")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }.accessibilityIdentifier("doneButton")
                }
            }
        }
    }
}

/// Timestamps of every tick, reading and command the core saw or sent.
struct ProbeLogSheet: View {
    let session: ShotSession
    @Environment(\.dismiss) private var dismiss
    @State private var filter = Filter.all

    enum Filter: String, CaseIterable, Identifiable {
        case all = "All", ticks = "Ticks", samples = "Samples", commands = "Commands"
        var id: String { rawValue }
        func includes(_ kind: UInt8) -> Bool {
            switch self {
            case .all: return true
            case .ticks: return kind == Probe.tick
            case .samples: return kind == Probe.sample
            case .commands: return kind == Probe.command || kind == Probe.state || kind == Probe.note
            }
        }
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 8) {
                let stats = session.probeStats
                Text("last 2000 entries: \(stats.ticks) ticks, \(stats.samples) samples, \(stats.commands) commands; longest tick gap \(stats.maxTickGapMs) ms")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .padding(.horizontal)
                    .accessibilityIdentifier("probeStats")
                Picker("Show", selection: $filter) {
                    ForEach(Filter.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                .padding(.horizontal)
                List(session.probeLines.filter { filter.includes($0.kind) }) { line in
                    Text(line.text)
                        .font(.system(.caption, design: .monospaced))
                        .listRowInsets(EdgeInsets(top: 1, leading: 12, bottom: 1, trailing: 12))
                }
                .listStyle(.plain)
            }
            .navigationTitle("Probe log")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }.accessibilityIdentifier("doneButton")
                }
            }
        }
        .task {
            while !Task.isCancelled {
                session.refreshProbe()
                try? await Task.sleep(for: .milliseconds(500))
            }
        }
    }
}

/// The switch from the simulated machine to a real Lunar and Bot.
struct DevicesSheet: View {
    let session: ShotSession
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Toggle("Real devices", isOn: Binding(
                        get: { session.realDevices }, set: { session.setRealDevices($0) }))
                        .accessibilityIdentifier("realDevicesToggle")
                    Text(session.transportStatus).foregroundStyle(.secondary)
                } footer: {
                    Text("Untested. The Bluetooth layer has never run against a real Lunar or SwitchBot Bot. It takes the first scale named like an Acaia and the first Bot it hears: leave this off while the deployed controller board is powered and holding them.")
                }
            }
            .navigationTitle("Devices")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }.accessibilityIdentifier("doneButton")
                }
            }
        }
    }
}
