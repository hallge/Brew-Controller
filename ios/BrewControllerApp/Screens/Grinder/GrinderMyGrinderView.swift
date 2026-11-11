// "My grinder" (web: grinderForm, grinderBody, saveGrinder): model, how it adjusts, divisor, unit, range,
// what it covers, zeroed date, the micro ring, the second grinder and sending to the shared library.
import SwiftUI

/// The form's state, and how it becomes the POST /api/grinder body (the web page's grinderBody).
struct GrinderFormDraft: Equatable {
    enum Kind: String, CaseIterable, Identifiable {
        case steps, dial, um
        var id: String { rawValue }
        var label: String {
            switch self {
            case .steps: return "Steps or clicks"
            case .dial: return "Stepless dial"
            case .um: return "Microns (display)"
            }
        }
    }

    /// "own:<id>", "lib:<id>" or "custom".
    var pick = "custom"
    var name = ""
    var kind: Kind = .steps
    var divisor = 1
    var unit = ""
    var minText = ""
    var maxText = ""
    var covers = 0
    var zeroDate = Date()
    var hasZeroDate = false
    var microOn = false
    var subStepsText = ""
    var rezeroed = false
    /// The grinder this form started from (for "unchanged" and the learned microns per step).
    var base = GrinderModel()

    static let divisors: [(Int, String)] = [
        (1, "Whole number (12)"), (2, "0.5 (12.5)"), (4, "0.25 (12.25)"), (5, "0.2 (12.2)"), (10, "0.1 (12.1)"),
        (20, "0.05 (12.05)"), (25, "0.04 (12.04)"), (50, "0.02 (12.02)"), (100, "0.01 (12.01)"),
    ]

    init() {}

    /// From the grinder in use.
    init(grinder g: GrinderModel) {
        base = g
        pick = g.modelId > 0 ? "own:\(g.modelId)" : (!g.libraryId.isEmpty ? "lib:\(g.libraryId)" : "custom")
        fill(model: g.model, unit: g.unit, divisor: g.divisor, subSteps: g.subSteps, min: g.min, max: g.max)
        covers = g.covers
        if let date = BrewFormat.date(fromYMD: g.zeroDate) {
            zeroDate = date
            hasZeroDate = true
        }
    }

    static func kindOf(unit: String, divisor: Int) -> Kind {
        unit == "um" ? .um : (divisor > 1 || unit == "dial") ? .dial : .steps
    }

    /// Fills the fields from a grinder or a catalogue model (the web page's gmFill).
    mutating func fill(model: String, unit: String, divisor: Int, subSteps: Int, min: Int, max: Int) {
        name = model
        kind = Self.kindOf(unit: unit, divisor: divisor)
        self.unit = (kind == .um || unit == (kind == .steps ? "step" : "dial")) ? "" : unit
        self.divisor = Swift.max(1, divisor)
        microOn = subSteps >= 2
        subStepsText = subSteps >= 2 ? String(subSteps) : ""
        if max > 0 {
            if subSteps >= 2 {
                minText = String((Swift.max(min, 1) - 1) / subSteps + 1)
                maxText = String((max + subSteps - 1) / subSteps)
            } else {
                let dial = BrewFormat.Dial(unit: unit, divisor: divisor, subSteps: 0)
                minText = dial.setting(Swift.max(min, 1))
                maxText = dial.setting(max)
            }
        } else {
            minText = ""
            maxText = ""
        }
    }

    var subSteps: Int { microOn ? Swift.max(0, Int(subStepsText) ?? 0) : 0 }

    /// How the form writes a setting (the web page's gmFmt).
    var dial: BrewFormat.Dial {
        let r = subSteps
        return BrewFormat.Dial(unit: "", divisor: kind == .dial && r < 2 ? divisor : 1, subSteps: r >= 2 ? r : 0)
    }

    /// The range typed, in stored steps; (0, 0) when not given or not sensible (the web page's gmRange).
    var range: (Int, Int) {
        let a = minText.trimmingCharacters(in: .whitespaces)
        let b = maxText.trimmingCharacters(in: .whitespaces)
        guard !b.isEmpty else { return (0, 0) }
        let r = subSteps
        var lo = 0
        var hi = 0
        if r >= 2 {
            lo = a.isEmpty ? 1 : ((Int(a) ?? 1) - 1) * r + 1
            hi = (Int(b) ?? 0) * r
        } else {
            let plain = BrewFormat.Dial(unit: "", divisor: dial.divisor, subSteps: 0)
            lo = a.isEmpty ? 1 : (GrinderSettingParser.parse(a, dial: plain) ?? 0)
            hi = GrinderSettingParser.parse(b, dial: plain) ?? 0
            if lo == 0, !a.isEmpty, Double(a.replacingOccurrences(of: ",", with: ".")) == 0 { lo = 1 }
        }
        return (lo > 0 && hi > lo && hi <= 2000) ? (lo, hi) : (0, 0)
    }

    var rangeUnreadable: Bool { !maxText.trimmingCharacters(in: .whitespaces).isEmpty && range.1 == 0 }

    private struct Source {
        var model: String
        var unit: String
        var umPerUnit: Double
        var divisor: Int
        var subSteps: Int
    }

    /// The body for POST /api/grinder. `models` and `shared` resolve the picked model.
    func body(models: [GrinderCatalogEntry], shared: [GrinderSharedEntry], asOther: Bool = false) -> [String: BrewJSON] {
        let own = pick.hasPrefix("own:") ? models.first { "own:\($0.id)" == pick } : nil
        let lib = pick.hasPrefix("lib:") ? shared.first { "lib:\($0.id)" == pick } : nil
        let source: Source? = own.map { Source(model: $0.model, unit: $0.unit, umPerUnit: $0.umPerUnit, divisor: $0.divisor, subSteps: $0.subSteps) }
            ?? lib.map { Source(model: $0.model, unit: $0.unit, umPerUnit: $0.umPerUnit, divisor: $0.divisor, subSteps: $0.subSteps) }
        let model = source?.model ?? name.trimmingCharacters(in: .whitespaces)
        let unitText = kind == .um ? "um" : (unit.trimmingCharacters(in: .whitespaces).isEmpty ? (kind == .dial ? "dial" : "step") : unit.trimmingCharacters(in: .whitespaces))
        let fmt = dial
        let same = source.map { unitText == $0.unit && fmt.subSteps == $0.subSteps && fmt.divisor == $0.divisor } ?? false
        let umPerUnit: Double
        if kind == .um {
            umPerUnit = 1
        } else if same, let source {
            umPerUnit = source.umPerUnit
        } else if source == nil, model == base.model, unitText == base.unit {
            umPerUnit = base.umPerUnit
        } else {
            umPerUnit = 0
        }
        let (lo, hi) = range
        let zero: Int
        if rezeroed {
            let parts = Calendar.current.dateComponents([.year, .month, .day], from: Date())
            zero = (parts.year ?? 0) * 10_000 + (parts.month ?? 0) * 100 + (parts.day ?? 0)
        } else if hasZeroDate {
            let parts = Calendar.current.dateComponents([.year, .month, .day], from: zeroDate)
            zero = (parts.year ?? 0) * 10_000 + (parts.month ?? 0) * 100 + (parts.day ?? 0)
        } else {
            zero = 0
        }
        var json: [String: BrewJSON] = [
            "modelId": .int(same ? (own?.id ?? 0) : 0),
            "libraryId": .string(same ? (lib?.id ?? "") : ""),
            "model": .string(model),
            "unit": .string(unitText),
            "umPerUnit": .double(umPerUnit),
            "divisor": .int(fmt.divisor),
            "subSteps": .int(fmt.subSteps),
            "min": .int(lo),
            "max": .int(hi),
            "covers": .int(covers),
            "zeroDate": .int(zero),
            "rezeroed": .bool(rezeroed && !asOther),
        ]
        if asOther { json["asOther"] = .bool(true) }
        return json
    }

    /// One line saying what the form means, as the controller will read it (the web page's gmSummary).
    func summary(burrs: String) -> String {
        var bits: [String] = []
        let word = kind == .um ? "µm" : (unit.isEmpty ? (kind == .dial ? "dial" : "step") : unit)
        let fmt = dial
        if microOn && fmt.subSteps < 2 {
            bits.append("Enter how many steps the micro adjustment has per macro step (2 to 99).")
        } else {
            let example = kind == .um ? 246 : fmt.subSteps >= 2 ? (12 - 1) * fmt.subSteps + 3 : fmt.divisor > 1 ? Int((12.5 * Double(fmt.divisor)).rounded()) : 12
            bits.append("Settings look like \(fmt.setting(example))" + (kind == .um ? " µm of burr gap" : " (\(word))"))
            let (lo, hi) = range
            if hi > 0 {
                bits.append(fmt.subSteps >= 2
                    ? "range \((lo - 1) / fmt.subSteps + 1)–\((hi + fmt.subSteps - 1) / fmt.subSteps) on the macro"
                    : "range \(fmt.setting(lo))–\(fmt.setting(hi))")
            } else if rangeUnreadable {
                bits.append("the range is not readable: finest must be below coarsest")
            }
            bits.append(kind == .um ? "one step is one micron" : "how far one step moves the shot is learned from your shots")
        }
        return bits.joined(separator: " · ") + "." + (burrs.isEmpty ? "" : " Burrs: \(burrs).")
    }
}

struct GrinderMyGrinderView: View {
    var store: GrinderStore
    @State private var form = GrinderFormDraft()
    @State private var loadedOnce = false
    @State private var confirmSave = false
    @State private var confirmRemove = false

    private var models: [GrinderCatalogEntry] { store.library.grinderModels }

    /// Library grinders that are not already one of the firmware's models.
    private var libraryOnly: [GrinderSharedEntry] {
        store.sharedGrinders.filter { entry in
            !models.contains { $0.model == entry.model || $0.libraryId == entry.id }
        }
    }

    private var pickItems: [(tag: String, name: String)] {
        let own = models.map { (tag: "own:\($0.id)", name: $0.model + ($0.umPerUnit > 0 ? "" : " (steps learned from your shots)")) }
        let lib = libraryOnly.map { (tag: "lib:\($0.id)", name: $0.model + " (shared library)") }
        return (own + lib).sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }

    private var customLabel: String {
        store.grinder.model.isEmpty ? "Not set yet: pick one, or type it below" : "Another grinder: type it below"
    }

    private var pickedName: String {
        pickItems.first { $0.tag == form.pick }?.name ?? (form.pick == "custom" && !form.name.isEmpty ? form.name : customLabel)
    }

    private var currentBurrs: String {
        if form.pick.hasPrefix("own:"), let m = models.first(where: { "own:\($0.id)" == form.pick }) { return m.burrs }
        if form.pick.hasPrefix("lib:"), let m = store.sharedGrinders.first(where: { "lib:\($0.id)" == form.pick }) { return m.burrs }
        return ""
    }

    private var changes: Bool {
        let g = store.grinder
        let body = form.body(models: models, shared: store.sharedGrinders)
        guard g.modelId > 0 || !g.model.isEmpty else { return false }
        let model: String = { if case .string(let s)? = body["model"] { return s } else { return "" } }()
        let unit: String = { if case .string(let s)? = body["unit"] { return s } else { return "" } }()
        return model != g.model || unit != g.unit || form.dial.divisor != g.divisor || form.subSteps != g.subSteps
    }

    var body: some View {
        Form {
            GrinderMessages(error: store.error, notice: store.notice)
            Section {
                NavigationLink {
                    GrinderModelPickerView(items: pickItems.map { GrinderPickItem(tag: $0.tag, name: $0.name) },
                                           selected: form.pick, customLabel: customLabel) { tag in
                        form.pick = tag
                        pickChanged(tag)
                    }
                } label: {
                    HStack {
                        Text("Model")
                        Spacer()
                        Text(pickedName).foregroundStyle(.secondary).lineLimit(1)
                    }
                }
                .accessibilityIdentifier("grinderModelPicker")
                if form.pick == "custom" {
                    TextField("Name, e.g. My hand grinder", text: $form.name)
                        .accessibilityIdentifier("grinderNameField")
                }
                Picker("How it adjusts", selection: $form.kind) {
                    ForEach(GrinderFormDraft.Kind.allCases) { Text($0.label).tag($0) }
                }
                .accessibilityIdentifier("grinderKindPicker")
                if form.kind == .dial && !form.microOn {
                    Picker("Read it to the nearest", selection: $form.divisor) {
                        ForEach(GrinderFormDraft.divisors, id: \.0) { Text($0.1).tag($0.0) }
                    }
                    .accessibilityIdentifier("grinderDivisorPicker")
                }
                if form.kind != .um {
                    TextField("A setting is called (optional), e.g. click", text: $form.unit)
                        .accessibilityIdentifier("grinderUnitField")
                }
                TextField(form.microOn ? "Finest macro setting" : "Finest setting", text: $form.minText)
                    .accessibilityIdentifier("grinderMinField")
                TextField(form.microOn ? "Coarsest macro setting, e.g. 60" : "Coarsest setting, e.g. 60", text: $form.maxText)
                    .accessibilityIdentifier("grinderMaxField")
                Picker("The range covers", selection: $form.covers) {
                    Text("Not said").tag(0)
                    Text("Espresso to filter").tag(2)
                    Text("Espresso only").tag(1)
                }
                Toggle("Zeroed, recalibrated or burrs changed on a date", isOn: $form.hasZeroDate)
                if form.hasZeroDate {
                    DatePicker("Last zeroed, recalibrated or burrs changed", selection: $form.zeroDate, displayedComponents: .date)
                }
                Toggle("Micro adjustment (a second ring or lever)", isOn: $form.microOn)
                    .accessibilityIdentifier("grinderMicroToggle")
                if form.microOn {
                    TextField("Steps per macro step, e.g. 9", text: $form.subStepsText).keyboardType(.numberPad)
                        .accessibilityIdentifier("grinderSubStepsField")
                }
                Text(form.summary(burrs: currentBurrs)).font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("grinderFormSummary")
            } header: {
                Text("My grinder")
            } footer: {
                Text("How far one step moves the shot is learned from your own shots, so burrs and microns per step are not needed. For a second ring or lever (macro and micro, like a Sette or Vario) add a micro adjustment. An inner burr you set once, like the Smart Grinder Pro's top burr, is not one: when you move it, press \"Zeroed today\" under Grinder upkeep.")
            }
            Section {
                Button("Save") { trySave() }
                    .accessibilityIdentifier("grinderSaveButton")
                if store.otherGrinderMissing {
                    Button("Add what is typed above as my second grinder") { Task { await addOther() } }
                        .accessibilityIdentifier("grinderAddOtherButton")
                }
                Text((store.grinder.model.isEmpty ? "No grinder named yet. " : "In use: \(store.grinder.model). ")
                     + "Changing how a listed model adjusts makes it your own version, which you can send to the shared list for review.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            shareSection
            otherSection
        }
        .navigationTitle("My grinder")
        .task {
            if !store.loaded { await store.refresh() }
            if !loadedOnce {
                form = GrinderFormDraft(grinder: store.grinder)
                loadedOnce = true
            }
        }
        .confirmationDialog("Grind settings from before will no longer be compared with new ones: grind advice starts fresh for every coffee, and if the unit changes the settings entered so far are cleared. To keep both grinders, use \"Add ... as my second grinder\" instead. Continue?",
                            isPresented: $confirmSave, titleVisibility: .visible) {
            Button("Save", role: .destructive) { Task { await save() } }
            Button("Cancel", role: .cancel) {}
        }
    }

    private func pickChanged(_ tag: String) {
        if let m = models.first(where: { "own:\($0.id)" == tag }) {
            form.fill(model: m.model, unit: m.unit, divisor: m.divisor, subSteps: m.subSteps, min: m.min ?? 0, max: m.max ?? 0)
        } else if let m = store.sharedGrinders.first(where: { "lib:\($0.id)" == tag }) {
            form.fill(model: m.model, unit: m.unit, divisor: m.divisor, subSteps: m.subSteps, min: m.min, max: m.max)
        }
    }

    private func trySave() {
        let (name, hasModel) = (form.name.trimmingCharacters(in: .whitespaces), form.pick != "custom")
        if !hasModel && name.isEmpty {
            store.error = "Pick a model, or type its name"
            return
        }
        if form.microOn && form.subSteps < 2 {
            store.error = "Enter how many steps the micro adjustment has per macro step, or remove it"
            return
        }
        if form.rangeUnreadable {
            store.error = "The range is not readable: the finest setting must be below the coarsest"
            return
        }
        if changes { confirmSave = true } else { Task { await save() } }
    }

    private func save() async {
        _ = await store.saveGrinder(form.body(models: models, shared: store.sharedGrinders), done: "Grinder saved")
    }

    private func addOther() async {
        let name = form.name.trimmingCharacters(in: .whitespaces)
        if form.pick == "custom" && name.isEmpty {
            store.error = "Pick a model, or type its name"
            return
        }
        let body = form.body(models: models, shared: store.sharedGrinders, asOther: true)
        if case .string(let model)? = body["model"], model == store.grinder.model {
            store.error = "That is the grinder already in use: choose or type the other one first"
            return
        }
        _ = await store.saveGrinder(body, done: "Second grinder added")
    }

    // MARK: Sharing

    private static let shareWords: [String: String] = [
        "local": "on this controller only", "queued": "waiting to send", "sent": "sent", "review": "in review",
        "published": "published", "declined": "declined", "refused": "refused by the library",
    ]

    @ViewBuilder
    private var shareSection: some View {
        let g = store.grinder
        if !g.model.isEmpty {
            Section("Shared library") {
                let known = g.modelId > 0
                Text(known ? "a known model" : (Self.shareWords[g.share] ?? g.share)).font(.subheadline).fontWeight(.semibold)
                Text(known ? "This model is in the firmware's own list: there is nothing to send." : shareMessage(g))
                    .font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("grinderShareMessage")
                if store.settings.shareEnabled && g.canShare {
                    Button(g.share == "local" ? "Send to the shared library" : g.share == "refused" ? "Send again" : "Send the new version") {
                        Task { await store.share(grinderId: g.id) }
                    }
                    .accessibilityIdentifier("grinderSendButton")
                }
            }
        }
    }

    private func shareMessage(_ g: GrinderModel) -> String {
        let base: String
        switch g.share {
        case "queued": base = "Signed and waiting in the outbox."
        case "sent": base = "Sent. The library has not answered yet."
        case "review": base = "The library has it. A reviewer has not decided yet."
        case "published": base = "In the shared list" + (g.libraryId.isEmpty ? "." : " as \(g.libraryId).")
        case "declined": base = "A reviewer did not accept it" + (g.reviewNote.isEmpty ? "" : ": \"\(g.reviewNote)\"") + ". What you typed is unchanged."
        case "refused": base = "The library's checks turned this record away" + (g.reviewNote.isEmpty ? "" : ": \"\(g.reviewNote)\"") + ". Correct it if needed, then send it again."
        default:
            base = store.settings.shareEnabled
                ? "Kept on this controller only. Sending it lets a reviewer add the model to the shared list."
                : "Kept on this controller only. Sharing is switched off (Setup)."
        }
        return base + (g.shareChanged ? " You have changed it since that version was sent." : "")
    }

    // MARK: Second grinder

    @ViewBuilder
    private var otherSection: some View {
        Section("Second grinder") {
            if let other = store.library.otherGrinder {
                Text("Your other grinder: \(other.model) (read in \(other.unit == "um" ? "µm" : other.unit)).")
                    .accessibilityIdentifier("grinderOtherName")
                Button("Use it instead") { Task { await store.switchGrinder(); form = GrinderFormDraft(grinder: store.grinder) } }
                    .accessibilityIdentifier("grinderUseOtherButton")
                if store.settings.shareEnabled && other.canShare {
                    Button("Send it to the shared library") { Task { await store.share(grinderId: other.id) } }
                }
                Button("Remove it", role: .destructive) { confirmRemove = true }
                    .accessibilityIdentifier("grinderRemoveOtherButton")
                    .confirmationDialog("Remove \(other.model)? Its settings for each coffee go with it; its shots stay in the history.",
                                        isPresented: $confirmRemove, titleVisibility: .visible) {
                        Button("Remove it", role: .destructive) { Task { await store.removeOtherGrinder() } }
                        Button("Cancel", role: .cancel) {}
                    }
            } else {
                Text("One grinder. A second can be kept beside it: choose or type it above, then \"Add ... as my second grinder\".")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }
}

extension GrinderStore {
    var otherGrinderMissing: Bool { library.otherGrinder == nil }
}
