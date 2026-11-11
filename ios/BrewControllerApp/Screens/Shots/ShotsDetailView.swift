// One shot (W: #sdetail, `showShot`): tiles, the curve with the best-rated shot of the same coffee and
// recipe as a dashed reference, the grind vote and grind edit, rating, taste, TDS and preparation, the
// comparison, and the shot's readings as CSV.
import SwiftUI

struct ShotsDetailView: View {
    let store: ShotsStore
    let shotID: Int

    @Environment(\.openURL) private var openURL

    @State private var curve: ShotDataModel?
    @State private var reference: ShotDataModel?
    @State private var referenceShot: ShotModel?
    @State private var curveMissing = false

    @State private var tdsText = ""
    @State private var doseText = ""
    @State private var purged = false
    @State private var uneven = false
    @State private var syncedFor: ShotModel?

    @State private var showGrindPrompt = false
    @State private var grindText = ""
    @State private var shareFile: ShotsSharedFile?
    @State private var askUndated = false

    private enum Field: Hashable { case tds, dose }
    @FocusState private var focus: Field?

    private var shot: ShotModel? { store.shot(id: shotID) }

    var body: some View {
        Group {
            if let shot {
                content(shot)
            } else {
                Text(store.loadedOnce ? "This shot is no longer in the history." : "Loading...")
                    .foregroundStyle(.secondary)
            }
        }
        .navigationTitle(shot.map { ShotsText.when($0) } ?? "Shot")
        .navigationBarTitleDisplayMode(.inline)
        .shotsThemed()
        .task(id: shotID) { await loadCurves() }
        .onAppear { sync() }
        .onChange(of: shot) { _, _ in sync() }
        .shotsShareSheet($shareFile)
        .shotsNotices(store)
    }

    @ViewBuilder
    private func content(_ shot: ShotModel) -> some View {
        List {
            Section {
                VStack(alignment: .leading, spacing: 2) {
                    Text(ShotsText.when(shot)).font(.title3.weight(.semibold))
                        .accessibilityIdentifier("shotDetailWhen")
                    Text((shot.coffee.isEmpty ? "" : shot.coffee + ", ") + shot.recipe)
                        .foregroundStyle(.secondary)
                        .accessibilityIdentifier("shotDetailName")
                }
                tiles(shot)
                    .listRowInsets(EdgeInsets(top: 8, leading: 12, bottom: 8, trailing: 12))
                    .listRowBackground(Color.clear)
            }

            Section {
                curveSection(shot)
            } footer: {
                curveNote(shot)
            }

            Section {
                Button(store.isComparing(shot.id) ? "Remove from comparison" : "Add to comparison") {
                    store.toggleCompare(shot.id)
                }
                .accessibilityIdentifier("shotCompareToggle")
                Picker("For grind advice", selection: voteBinding(shot)) {
                    Text("Auto").tag(0)
                    Text("Count").tag(2)
                    Text("Ignore").tag(1)
                }
                .pickerStyle(.segmented)
                .accessibilityIdentifier("shotVote")
            } footer: {
                Text("Auto lets the controller decide whether the shot counts toward the grind advice. Count uses it anyway; Ignore leaves it out.")
            }

            Section("How did it taste?") {
                tasteRow(shot)
                ratingRow(shot)
                tdsRow(shot)
            }

            Section {
                preparationRows(shot)
            } header: {
                Text("Preparation")
            }

            Section {
                Button("Download this shot's readings") { Task { await downloadReadings(shot) } }
                    .accessibilityIdentifier("shotCSV")
                Button("Send to Beanconqueror") { sendToBeanconqueror(shot) }
                    .accessibilityIdentifier("shotBeanconqueror")
            }
        }
        .accessibilityIdentifier("shotDetail")
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("Done") { focus = nil }
            }
        }
        .onChange(of: focus) { old, _ in
            if old == .tds { Task { await commitTDS() } }
            if old == .dose { Task { await commitPrep() } }
        }
        .alert("Grind setting", isPresented: $showGrindPrompt) {
            TextField("Setting", text: $grindText)
                .keyboardType(.numbersAndPunctuation)
            Button("Save") { Task { await saveGrind() } }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Grind setting for this shot, in \(unitName) (0 = not recorded)")
        }
        .confirmationDialog(
            "This shot has no date (the controller's clock was not set). Send it to Beanconqueror dated now?",
            isPresented: $askUndated, titleVisibility: .visible
        ) {
            Button("Send dated now") { openBeanconqueror(shot, now: Date()) }
        }
    }

    // MARK: Tiles

    private func tiles(_ shot: ShotModel) -> some View {
        let target = ShotsText.stopTarget(shot)
        let targetNote = "target " + BrewFormat.grams(target)
            + (target != shot.targetWeightG ? " (recipe " + BrewFormat.grams(shot.targetWeightG) + ")" : "")
            + ", drip " + BrewFormat.grams(shot.dripWeightG)
        let grindValue = shot.grindUm > 0 ? store.dial.settingUnit(shot.grindUm) : "--"
        let flows = [shot.flow25GS, shot.flow50GS, shot.flow75GS].map { BrewFormat.grams($0) }.joined(separator: " / ")
        var ended = "offset " + BrewFormat.flow(shot.stopOffsetG) + " → " + BrewFormat.flow(shot.nextStopOffsetG) + " g"
        if shot.bloomPauseS > 0 { ended += ", bloom " + BrewFormat.decimal(shot.bloomPauseS, 0) + " s" }
        return LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
            ShotsTile(title: "In the cup", value: BrewFormat.grams(shot.finalWeightG) + " g", detail: targetNote)
            ShotsTile(title: "Brew time", value: BrewFormat.seconds(shot.shotTimeS) + " s",
                      detail: shot.targetTimeS > 0 ? "target " + BrewFormat.decimal(shot.targetTimeS, 1) : "")
            ShotsTile(title: "Pour", value: BrewFormat.seconds(shot.pourTimeS) + " s",
                      detail: BrewFormat.flow(shot.pourFlowGS) + " g/s, first drops " + BrewFormat.seconds(shot.firstDropS) + " s")
            VStack(alignment: .leading, spacing: 4) {
                ShotsTile(title: "Grind", value: grindValue, detail: BrewFormat.decimal(shot.doseG, 1) + " g dose")
                Button("change") {
                    grindText = shot.grindUm > 0 ? store.dial.setting(shot.grindUm) : ""
                    showGrindPrompt = true
                }
                .font(.caption)
                .accessibilityIdentifier("shotGrindChange")
            }
            ShotsTile(title: "Flow at 25/50/75 %", value: flows, detail: "peak " + BrewFormat.flow(shot.peakFlowGS) + " g/s")
            ShotsTile(title: "Ended by", value: ShotsText.endReason(shot), detail: ended)
        }
    }

    // MARK: Curve

    @ViewBuilder
    private func curveSection(_ shot: ShotModel) -> some View {
        if let curve {
            ShotsCurveChart(curve: curve, reference: reference, targetG: ShotsText.stopTarget(shot),
                            firstDropS: shot.firstDropS, stopS: shot.shotTimeS)
        } else if !shot.hasData || curveMissing {
            Text("The readings of this shot are no longer stored; the summary is.")
                .foregroundStyle(.secondary)
                .accessibilityIdentifier("shotNoCurve")
        } else {
            HStack { ProgressView(); Text("Reading the curve...").foregroundStyle(.secondary) }
        }
    }

    @ViewBuilder
    private func curveNote(_ shot: ShotModel) -> some View {
        if let curve {
            Text(curveNoteText(curve))
                .accessibilityIdentifier("shotDataNote")
        }
    }

    /// The legend and the recording statistics under the chart (WebPage.h `showShot`, `sdata`).
    private func curveNoteText(_ curve: ShotDataModel) -> String {
        let stats = curve.data
        var text = "White: weight. Orange: flow."
        if let best = referenceShot {
            text += " Dashed green: your best-rated shot of this coffee ("
            text += ShotsText.stars(best.rating) + ", " + BrewFormat.seconds(best.shotTimeS) + " s)."
        }
        text += " \(stats.samples) readings over " + BrewFormat.decimal(stats.durationS, 1) + " s,"
        text += " one every " + BrewFormat.decimal(stats.meanPeriodMs, 0) + " ms,"
        text += " longest gap " + BrewFormat.decimal(stats.maxGapMs, 0) + " ms."
        if stats.timerSamples > 0 {
            text += " Scale timer in \(stats.timerSamples) of them"
            if stats.timerSpanS > 0 {
                text += ": it counted " + BrewFormat.decimal(stats.timerSpanS, 1) + " s while this controller counted "
                text += BrewFormat.decimal(stats.clockSpanS, 1) + " s."
            } else {
                text += "."
            }
        } else {
            text += " The scale sent no timer."
        }
        return text
    }

    private func loadCurves() async {
        curve = nil
        reference = nil
        referenceShot = nil
        curveMissing = false
        guard let shot, shot.hasData else { return }
        guard let data = await store.curve(id: shotID) else {
            curveMissing = true
            return
        }
        curve = data
        if let best = store.referenceShot(for: shot), let ref = await store.curve(id: best.id) {
            reference = ref
            referenceShot = best
        }
    }

    // MARK: Vote, taste, rating, TDS

    private func voteBinding(_ shot: ShotModel) -> Binding<Int> {
        Binding(
            get: { shot.grindVote == "count" ? 2 : shot.grindVote == "ignore" ? 1 : 0 },
            set: { value in Task { await store.vote(shot.id, value: value) } })
    }

    private func tasteRow(_ shot: ShotModel) -> some View {
        let current = ShotsText.tastes.firstIndex(of: shot.taste) ?? 0
        return HStack {
            ForEach(1..<ShotsText.tastes.count, id: \.self) { index in
                let word = ShotsText.tastes[index]
                Button(word.capitalized) {
                    Task { await store.rate(shot.id, rating: shot.rating, taste: current == index ? 0 : index) }
                }
                .buttonStyle(.bordered)
                .tint(current == index ? BrewTheme.shared.palette.accent : Color.secondary)
                .accessibilityIdentifier("shotTaste-\(word)")
                .accessibilityAddTraits(current == index ? .isSelected : [])
            }
        }
        .frame(maxWidth: .infinity)
    }

    private func ratingRow(_ shot: ShotModel) -> some View {
        let current = ShotsText.tastes.firstIndex(of: shot.taste) ?? 0
        return HStack {
            Text("Rating")
            Spacer()
            ForEach(1...5, id: \.self) { star in
                Button {
                    Task { await store.rate(shot.id, rating: shot.rating == star ? 0 : star, taste: current) }
                } label: {
                    Image(systemName: star <= shot.rating ? "star.fill" : "star")
                        .foregroundStyle(star <= shot.rating ? BrewTheme.shared.palette.accent : Color.secondary)
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("\(star) of 5")
                .accessibilityIdentifier("shotStar-\(star)")
            }
        }
    }

    private func tdsRow(_ shot: ShotModel) -> some View {
        HStack {
            Text("TDS %")
            Spacer()
            TextField("--", text: $tdsText)
                .keyboardType(.decimalPad)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 80)
                .focused($focus, equals: .tds)
                .accessibilityLabel("TDS percent from a refractometer")
                .accessibilityIdentifier("shotTDS")
            if let extraction = shot.extractionPct, extraction > 0 {
                Text("extraction " + BrewFormat.decimal(extraction, 1) + " %")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func commitTDS() async {
        guard let shot else { return }
        let text = tdsText.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
        if text.isEmpty {
            if (shot.tdsPct ?? 0) > 0 { await store.setTDS(shot.id, tds: 0) }
            return
        }
        guard let value = Double(text), value >= 0.5, value <= 25 else {
            store.errorText = "TDS is a percent from 0.5 to 25"
            return
        }
        if abs(value - (shot.tdsPct ?? 0)) > 0.001 { await store.setTDS(shot.id, tds: value) }
    }

    // MARK: Preparation

    @ViewBuilder
    private func preparationRows(_ shot: ShotModel) -> some View {
        HStack {
            Text("Dose weighed")
            Spacer()
            TextField(shot.doseG > 0 ? BrewFormat.grams(shot.doseG) : "", text: $doseText)
                .keyboardType(.decimalPad)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 80)
                .focused($focus, equals: .dose)
                .accessibilityLabel("Dose weighed in grams")
                .accessibilityIdentifier("shotDoseWeighed")
            Text("g").foregroundStyle(.secondary)
        }
        Toggle("Purged first", isOn: Binding(get: { purged }, set: { purged = $0; Task { await commitPrep() } }))
            .accessibilityIdentifier("shotPurged")
        Toggle("Flow looked uneven", isOn: Binding(get: { uneven }, set: { uneven = $0; Task { await commitPrep() } }))
            .accessibilityIdentifier("shotUneven")
    }

    private func commitPrep() async {
        guard let shot else { return }
        let text = doseText.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
        let dose = text.isEmpty ? 0 : (Double(text) ?? -1)
        guard dose == 0 || (dose >= 5 && dose <= 40) else {
            store.errorText = "Dose weighed: 5 to 40 g, or leave it empty"
            return
        }
        let unchanged = abs(dose - shot.doseWeighedG) < 0.001 && purged == shot.purged && uneven == shot.uneven
        if unchanged { return }
        await store.setPrep(shot.id, dose: dose, purged: purged, uneven: uneven)
    }

    // MARK: Grind

    private var unitName: String {
        let unit = store.library?.grinder.unit ?? "um"
        return unit == "um" ? "µm" : unit
    }

    private func saveGrind() async {
        let text = grindText.trimmingCharacters(in: .whitespaces)
        let steps: Int
        if text.isEmpty || text == "0" {
            steps = 0
        } else if let parsed = ShotsGrind.parse(text, dial: store.dial) {
            steps = parsed
        } else {
            store.errorText = "That is not a setting this grinder can have"
            return
        }
        await store.setGrind(shotID, steps: steps)
    }

    // MARK: Files and Beanconqueror

    private func downloadReadings(_ shot: ShotModel) async {
        guard let file = await store.downloadShotCSV(id: shot.id) else { return }
        shareFile = ShotsSharedFile.make(file)
    }

    private func sendToBeanconqueror(_ shot: ShotModel) {
        if shot.timestamp.isEmpty || shot.date == nil {
            askUndated = true
        } else {
            openBeanconqueror(shot, now: nil)
        }
    }

    private func openBeanconqueror(_ shot: ShotModel, now: Date?) {
        let coffee = store.library?.coffee(id: shot.coffeeId)
        let grind = shot.grindUm > 0 ? store.dial.setting(shot.grindUm) : ""
        guard let url = CoffeeBeanconqueror.link(shot: shot, data: curve, coffee: coffee,
                                                 grinderModel: store.library?.grinder.model ?? "",
                                                 grindText: grind, now: now) else {
            store.errorText = "The shot could not be prepared for Beanconqueror."
            return
        }
        store.announce("Opening Beanconqueror... if nothing happens, install it and open it once first.")
        openURL(url)
    }

    // MARK: Sync

    /// Copies the shot's stored values into the fields, unless the owner is typing in one.
    private func sync() {
        guard let shot, shot != syncedFor else { return }
        syncedFor = shot
        if focus != .tds { tdsText = shot.tdsPct.map { BrewFormat.decimal($0, 2) } ?? "" }
        if focus != .dose { doseText = shot.doseWeighedG > 0 ? BrewFormat.grams(shot.doseWeighedG) : "" }
        purged = shot.purged
        uneven = shot.uneven
    }
}
