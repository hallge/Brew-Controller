// Make and model (web: loadMachine, fillMachine, saveMachine, shareMachine): the machine in use, a
// built-in, a shared-library machine or one typed in; how a shot is run; sending to the shared library.
import SwiftUI

/// The form's fields (web: the mc_* inputs).
struct MachineDraft: Equatable {
    var maker = ""
    var model = ""
    var control = ""
    var style = "toggle_button"
    var manualStart = "tap"
    var holdPreinfusion = false
    var tapPreinfusion = false
    var powerToggle = false
    var selfStopText = "0"
    var autoOffText = "0"
    /// The profile the fields were filled from (decides what is sent).
    var selected = MachineProfile()

    init() {}

    init(profile c: MachineProfile) {
        fill(c)
    }

    mutating func fill(_ c: MachineProfile) {
        maker = c.maker
        model = c.model
        control = c.control
        style = c.style.isEmpty ? "toggle_button" : c.style
        manualStart = c.manualStart.isEmpty ? "tap" : c.manualStart
        holdPreinfusion = c.holdPreinfusion
        tapPreinfusion = c.tapPreinfusion
        powerToggle = c.powerToggle
        selfStopText = String(c.selfStopS)
        autoOffText = String(c.autoOffMin)
        selected = c
    }

    var selfStopS: Int { min(600, max(0, Int(selfStopText) ?? 0)) }
    var autoOffMin: Int { min(1440, max(0, Int(autoOffText) ?? 0)) }

    /// The body for POST /api/machine (the web page's saveMachine): a built-in is sent by number, a library
    /// machine by id, anything else in full.
    var body: [String: BrewJSON] {
        let s = selected
        let same = s.maker == maker.trimmingCharacters(in: .whitespaces)
            && s.model == model.trimmingCharacters(in: .whitespaces)
            && s.control == control.trimmingCharacters(in: .whitespaces)
            && s.style == style
            && (s.manualStart.isEmpty ? "tap" : s.manualStart) == manualStart
            && s.holdPreinfusion == holdPreinfusion && s.tapPreinfusion == tapPreinfusion
            && s.powerToggle == powerToggle && s.selfStopS == selfStopS
        if same, s.builtin > 0 {
            return ["builtin": .int(s.builtin), "auto_off_min": .int(autoOffMin)]
        }
        var json: [String: BrewJSON] = [
            "maker": .string(maker.trimmingCharacters(in: .whitespaces)),
            "model": .string(model.trimmingCharacters(in: .whitespaces)),
            "control": .string(control.trimmingCharacters(in: .whitespaces)),
            "style": .string(style),
            "manual_start": .string(manualStart),
            "hold_preinfusion": .bool(holdPreinfusion),
            "tap_preinfusion": .bool(tapPreinfusion),
            "power_toggle": .bool(powerToggle),
            "self_stop_s": .int(selfStopS),
            "auto_off_min": .int(autoOffMin),
        ]
        if same, !s.libraryId.isEmpty { json["library_id"] = .string(s.libraryId) }
        return json
    }
}

struct MachineMakeModelView: View {
    var store: MachineStore
    @State private var draft = MachineDraft()
    @State private var pick = ""
    @State private var loadedOnce = false

    private var current: MachineProfile { store.machine.current }

    /// The firmware's own profile when it is not the one in use.
    private var firstBuiltin: MachineProfile? {
        guard let b = store.machine.builtins.first, current.builtin != 1 else { return nil }
        return b
    }

    private func name(_ p: MachineProfile) -> String { "\(p.maker) \(p.model) - \(p.control)" }

    private var stateText: String {
        let c = current
        let how: String
        if c.builtin > 0 {
            how = "comes with the firmware"
        } else {
            switch c.shareState ?? "local" {
            case "local": how = "yours, on this controller only"
            case "queued": how = "yours, waiting to be sent"
            case "sent": how = "yours, sent to the library"
            case "review": how = "yours, with a reviewer"
            case "published": how = "in the shared library as \(c.libraryId)"
            case "declined": how = "not accepted by the library"
            case "refused": how = "refused by the library's checks"
            default: how = "yours"
            }
        }
        var text = "In use: \(c.maker) \(c.model), \(c.control) (\(how))"
        if (c.shareState == "declined" || c.shareState == "refused"), !store.reviewNote.isEmpty {
            text += ". Reviewer: \(store.reviewNote)"
        }
        if c.builtin == 0, c.changedSinceShared == true {
            text += ". Changed since it was sent: send it again to update the entry."
        }
        return text
    }

    var body: some View {
        Form {
            MachineMessages(error: store.error, notice: store.notice)
            Section {
                Text(stateText).font(.footnote).accessibilityIdentifier("machineInUse")
                Picker("Machine", selection: $pick) {
                    Text("\(name(current)) (in use\(current.builtin > 0 ? "" : ", yours"))").tag("")
                    if let b = firstBuiltin {
                        Text("\(name(b)) (as it comes with the firmware)").tag("b0")
                    }
                    ForEach(Array(store.shared.enumerated()), id: \.offset) { index, entry in
                        if entry.id != current.libraryId || current.libraryId.isEmpty {
                            Text("\(entry.maker) \(entry.model) - \(entry.control) (shared library)").tag("l\(index)")
                        }
                    }
                    Text("Another machine: type it below").tag("new")
                }
                .accessibilityIdentifier("machinePicker")
                .onChange(of: pick) { _, value in pickChanged(value) }
                TextField("Maker", text: $draft.maker).accessibilityIdentifier("machineMakerField")
                TextField("Model", text: $draft.model).accessibilityIdentifier("machineModelField")
                TextField("Control", text: $draft.control).accessibilityIdentifier("machineControlField")
                Picker("How a shot is run", selection: $draft.style) {
                    ForEach(MachineStyle.allCases) { Text($0.label).tag($0.rawValue) }
                }
                .accessibilityIdentifier("machineStylePicker")
                if draft.style == "manual" {
                    Picker("By hand, the clock starts", selection: $draft.manualStart) {
                        Text("when Start is tapped").tag("tap")
                        Text("with the first coffee on the scale").tag("first_drip")
                    }
                }
                Toggle("Holding the button pre-infuses", isOn: $draft.holdPreinfusion)
                Toggle("A tap uses its own pre-infusion", isOn: $draft.tapPreinfusion)
                Toggle("Power button toggles", isOn: $draft.powerToggle)
                HStack {
                    Text("Stops a shot itself after (s, 0 never)")
                    Spacer()
                    TextField("0", text: $draft.selfStopText).keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing).frame(width: 70)
                        .accessibilityIdentifier("machineSelfStopField")
                }
                HStack {
                    Text("Switches itself off after (minutes idle, 0 never)")
                    Spacer()
                    TextField("0", text: $draft.autoOffText).keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing).frame(width: 70)
                        .accessibilityIdentifier("machineAutoOffField")
                }
            } header: {
                Text("Make and model")
            } footer: {
                Text("The controller cannot see the machine. After this long with nothing done on it, it counts the machine as off, so a scheduled or asked-for \"on\" presses the button again. Breville Dual Boiler: 240 (it switches itself off after 4 hours unused), or 0.")
            }
            Section {
                Button("Save") { Task { await store.saveMachine(draft.body) } }
                    .accessibilityIdentifier("machineSaveButton")
                if current.canShare == true && store.settings.shareEnabled {
                    Button(current.shareState == "refused" ? "Send again"
                           : (current.shareState ?? "local") != "local" ? "Send the new version" : "Send to the shared library") {
                        Task { await store.shareMachine() }
                    }
                    .accessibilityIdentifier("machineShareButton")
                }
            }
        }
        .navigationTitle("Make and model")
        .task {
            if !store.loaded { await store.refresh() }
            if !loadedOnce {
                draft = MachineDraft(profile: store.machine.current)
                loadedOnce = true
            }
        }
    }

    private func pickChanged(_ value: String) {
        if value.isEmpty {
            draft = MachineDraft(profile: current)
        } else if value == "new" {
            var fresh = MachineDraft()
            fresh.style = draft.style
            fresh.manualStart = draft.manualStart
            draft = fresh
        } else if value == "b0", let b = store.machine.builtins.first {
            draft.fill(b)
        } else if value.hasPrefix("l"), let index = Int(value.dropFirst()), index < store.shared.count {
            var profile = store.shared[index].profile
            profile.builtin = 0
            draft.fill(profile)
        }
    }
}
