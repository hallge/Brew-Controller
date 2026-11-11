// The Grinder tab (web: #grinder). Root of the tab: the shell puts it in a NavigationStack.
import SwiftUI

struct GrinderTab: View {
    @State private var store = GrinderStore()
    @State private var settingText = ""
    @State private var settingHint: String?
    @State private var showAllCoffees = true
    @State private var measured = GrinderMeasured()

    var body: some View {
        List {
            GrinderMessages(error: store.error, notice: store.notice)
            settingSection
            adviceSection
            chartsSection
            NavigationLink {
                GrinderAdviceSettingsView(store: store)
            } label: {
                Label("How advice is worked out", systemImage: "slider.horizontal.3")
            }
            .accessibilityIdentifier("grinderAdviceSettingsLink")
            upkeepSection
            NavigationLink {
                GrinderMyGrinderView(store: store)
            } label: {
                Label("My grinder", systemImage: "gearshape.2")
            }
            .accessibilityIdentifier("grinderMyGrinderLink")
            sharedSection
        }
        .navigationTitle("Grinder")
        .accessibilityIdentifier("grinderScreen")
        .task {
            await store.refresh()
            if let value = try? await BrewAPI.shared.get("/api/grind", as: GrinderMeasured.self) { measured = value }
        }
        .refreshable { await store.refresh() }
    }

    // MARK: Setting now

    private var settingSection: some View {
        Section("Setting now") {
            HStack(alignment: .firstTextBaseline) {
                Text(store.advice.currentUm > 0 ? store.dial.setting(store.advice.currentUm) : "--")
                    .font(.system(size: 40, weight: .light, design: .rounded))
                    .accessibilityIdentifier("grinderSettingNow")
                Text(store.dial.isRinged ? "" : store.dial.unit).foregroundStyle(.secondary)
                Spacer()
                if !store.advice.recipe.isEmpty {
                    Text("Advice for \(store.advice.recipe)\(store.advice.coffee.isEmpty ? "" : ", " + store.advice.coffee)")
                        .font(.caption).foregroundStyle(.secondary).multilineTextAlignment(.trailing)
                }
            }
            if let gap = measured.gapUm {
                Text("The grinder measured \(BrewFormat.decimal(gap, 1)) µm"
                     + (measured.doseG.map { ", \(BrewFormat.decimal($0, 2)) g" } ?? "")
                     + " at its last grind (\(Int((measured.ageS ?? 0) / 60)) min ago).")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            HStack {
                TextField("I moved the grinder to (\(store.dial.isRinged ? "outer:inner" : store.dial.unit))", text: $settingText)
                    .keyboardType(store.dial.isRinged ? .numbersAndPunctuation : .decimalPad)
                    .accessibilityIdentifier("grinderSettingField")
                Button("Set") { Task { await setFromField() } }
                    .buttonStyle(.bordered)
                    .accessibilityIdentifier("grinderSetButton")
            }
            if let settingHint {
                Text(settingHint).font(.footnote).foregroundStyle(.red)
            }
            Text("Shots are only compared with others made at the same setting.")
                .font(.footnote).foregroundStyle(.secondary)
        }
    }

    private func setFromField() async {
        guard let um = GrinderSettingParser.parse(settingText, dial: store.dial) else {
            settingHint = "That is not a setting this grinder can have (\(GrinderSettingParser.hint(dial: store.dial)))"
            return
        }
        settingHint = nil
        if await store.setGrind(um) { settingText = "" }
    }

    // MARK: Advice

    private var adviceSection: some View {
        let text = GrinderAdviceText.make(store.advice, dial: store.dial)
        return Section("Advice") {
            Text(text.title)
                .font(.headline)
                .foregroundStyle(text.tone == .ok ? Color.green : text.tone == .warn ? Color.orange : Color.primary)
                .accessibilityIdentifier("grinderAdvicePill")
            if !text.why.isEmpty {
                Text(text.why).font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("grinderAdviceWhy")
            }
            let age = GrinderAdviceText.beanAge(store.advice)
            if !age.isEmpty { Text(age).font(.footnote).foregroundStyle(.secondary) }
            if let label = text.applyLabel, let um = text.applyUm {
                Button(label) { Task { _ = await store.setGrind(um) } }
                    .buttonStyle(.borderedProminent)
                    .accessibilityIdentifier("grinderApplyButton")
            }
        }
    }

    // MARK: Charts

    private var chartsSection: some View {
        let points = GrinderChartData.points(store.shots, calibration: store.grinder.calibration,
                                             pourBasis: store.settings.grindTimeBasis == 1)
        let shown = showAllCoffees ? points : points.filter { $0.coffee == store.advice.coffee }
        return Group {
            Section("Brew time against grind setting") {
                Toggle("All coffees", isOn: $showAllCoffees).accessibilityIdentifier("grinderAllCoffees")
                if !store.shotsAvailable {
                    Text("The shots cannot be read while a shot is running.").font(.footnote).foregroundStyle(.secondary)
                } else if shown.isEmpty {
                    Text("No shots to show yet: the chart fills as shots are stopped on the weight on this grinder.")
                        .font(.footnote).foregroundStyle(.secondary)
                } else {
                    GrinderScatterChart(points: shown, dial: store.dial, targetS: store.advice.targetTimeS,
                                        toleranceS: store.advice.toleranceS, currentCoffee: store.advice.coffee)
                    if store.advice.umPerS > 0 {
                        Text("Sensitivity: \(BrewFormat.decimal(store.advice.umPerS, 2)) \(store.dial.unit) per second")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
            Section("Grind setting, shot by shot") {
                if shown.isEmpty {
                    Text("Each shot's setting appears here, oldest first.").font(.footnote).foregroundStyle(.secondary)
                } else {
                    GrinderShotsChart(points: shown, dial: store.dial, targetS: store.advice.targetTimeS,
                                      toleranceS: store.advice.toleranceS)
                }
            }
        }
    }

    // MARK: Upkeep

    private var upkeepSection: some View {
        Section("Grinder upkeep") {
            Text("\(store.maintenance.shotsTotal) shots counted").font(.footnote).foregroundStyle(.secondary)
            MachineUpkeepRow(job: store.job("grinder_clean"), title: "Clean the grinder", idPrefix: "grinderClean",
                             onDone: { await store.upkeepDone("grinder_clean") },
                             onEvery: { await store.upkeepEvery("grinder_clean", $0) })
            VStack(alignment: .leading, spacing: 4) {
                Text("Zero the grinder").fontWeight(.semibold)
                let since = store.shots.filter { $0.grinderCalibration == store.grinder.calibration }.count
                Text("\(since) shots since" + (store.grinder.zeroDate > 0 ? ", last zeroed \(Self.dateText(store.grinder.zeroDate))" : ", never recorded"))
                    .font(.footnote).foregroundStyle(.secondary)
                Button("Zeroed today") { Task { await zeroedToday() } }
                    .buttonStyle(.bordered)
                    .accessibilityIdentifier("grinderZeroedButton")
            }
            .confirmationDialog("Grind settings from before will no longer be compared with new ones: grind advice starts fresh for every coffee, and if the unit changes the settings entered so far are cleared. To keep both grinders, use \"Add ... as my second grinder\" instead. Continue?",
                                isPresented: $confirmZero, titleVisibility: .visible) {
                Button("Zeroed today", role: .destructive) { Task { await reallyZero() } }
                Button("Cancel", role: .cancel) {}
            }
        }
    }

    @State private var confirmZero = false

    private func zeroedToday() async { confirmZero = true }

    private func reallyZero() async {
        var form = GrinderFormDraft(grinder: store.grinder)
        form.rezeroed = true
        _ = await store.saveGrinder(form.body(models: store.library.grinderModels, shared: store.sharedGrinders),
                                    done: "Grinder saved")
    }

    static func dateText(_ ymd: Int) -> String {
        guard let date = BrewFormat.date(fromYMD: ymd) else { return "" }
        return date.formatted(date: .abbreviated, time: .omitted)
    }

    // MARK: Shared list

    private var sharedSection: some View {
        Section("Shared grinder list") {
            if store.sharedGrinders.isEmpty {
                Text("The shared library has no grinders listed yet. The model list holds the firmware's own grinders and those the shared library has published.")
                    .font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("grinderSharedEmpty")
            }
            ForEach(store.sharedGrinders) { entry in
                VStack(alignment: .leading, spacing: 2) {
                    Text(entry.model)
                    let dial = BrewFormat.Dial(unit: entry.unit, divisor: entry.divisor, subSteps: entry.subSteps)
                    Text([entry.burrs, entry.max > 0 ? "range \(dial.setting(entry.min == 0 ? 1 : entry.min))–\(dial.setting(entry.max))" : "",
                          entry.umPerUnit > 0 ? "one step is \(BrewFormat.decimal(entry.umPerUnit, 1)) µm" : ""]
                        .filter { !$0.isEmpty }.joined(separator: " · "))
                        .font(.footnote).foregroundStyle(.secondary)
                }
            }
        }
    }
}

/// An error in red and a notice in green, at the top of a list.
struct GrinderMessages: View {
    var error: String?
    var notice: String?

    var body: some View {
        if let error {
            Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("grinderError") }
        } else if let notice {
            Section { Text(notice).foregroundStyle(.green).accessibilityIdentifier("grinderNotice") }
        }
    }
}

/// What the grinder add-on measured at its last grind (GET /api/grind: grinder_gap_um, grinder_dose_g,
/// grinder_age_s; only with an add-on, so mostly absent).
struct GrinderMeasured: Codable, Sendable, Equatable {
    var gapUm: Double?
    var doseG: Double?
    var ageS: Double?

    enum CodingKeys: String, CodingKey {
        case gapUm = "grinder_gap_um", doseG = "grinder_dose_g", ageS = "grinder_age_s"
    }
}

extension GrinderMeasured {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        gapUm = c.lenient(.gapUm)
        doseG = c.lenient(.doseG)
        ageS = c.lenient(.ageS)
    }
}
