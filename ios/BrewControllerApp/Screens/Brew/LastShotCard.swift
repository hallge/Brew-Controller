// The web page's "most recent shot" on Home (showShot with where = 'h'): the numbers against their
// targets, the line of what happened, how far it was off, the curve (weight, flow, and a dashed
// reference: the shot you rated best of this coffee and recipe), taste, stars and TDS, the grind vote,
// the preparation, Send to Beanconqueror, Share to Reddit and a link to the shot's own screen. It is
// built from /api/shots, so it shows before the curve is loaded and when the curve is gone.
import Charts
import SwiftUI
import UIKit

struct BrewLastShotCard: View {
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        BrewCard {
            VStack(alignment: .leading, spacing: 12) {
                if let shot = model.lastShot {
                    BrewLastShotContent(shot: shot)
                } else {
                    Text("MOST RECENT SHOT").font(.subheadline).foregroundStyle(brewPal.dim)
                    Text(model.shotsLoaded ? "No shot yet. Brew one and it appears here." : "Loading the shots…")
                        .foregroundStyle(brewPal.dim)
                        .accessibilityIdentifier("lastShotEmpty")
                }
            }
        }
        .accessibilityIdentifier("lastShotCard")
    }
}

private struct BrewLastShotContent: View {
    var shot: ShotModel
    private var model: BrewModel { BrewModel.shared }
    @Environment(\.openURL) private var openURL
    @State private var tdsText = ""
    @State private var doseText = ""
    @State private var purged = false
    @State private var uneven = false
    @State private var askUndated = false
    @State private var prepError: String?

    private static let tastes = ["", "sour", "balanced", "bitter"]
    private static let tasteTitles = ["—", "Sour", "Balanced", "Bitter"]

    private var tasteIndex: Int { Self.tastes.firstIndex(of: shot.taste) ?? 0 }
    private var stop: Double { BrewShotWords.stopTarget(shot) }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Text("MOST RECENT SHOT").font(.subheadline).foregroundStyle(brewPal.dim)
                Spacer()
                Button("Open") { BrewNavigator.shared.open(.shot(shot.id)) }
                    .font(.subheadline)
                    .accessibilityIdentifier("lastShotOpen")
            }
            numbers
            Text(BrewShotWords.detail(shot)).font(.footnote).foregroundStyle(brewPal.dim)
                .accessibilityIdentifier("lastShotDetail")
            Text(BrewShotWords.verdict(shot)).font(.subheadline.weight(.semibold)).foregroundStyle(brewPal.text)
                .accessibilityIdentifier("lastShotVerdict")
            curve
            tasteAndStars
            tdsRow
            voteRow
            prepRow
            actions
        }
        .onAppear(perform: sync)
        .onChange(of: syncKey) { _, _ in sync() }
        .confirmationDialog("This shot has no date (the controller's clock was not set). Send it to Beanconqueror dated now?",
                            isPresented: $askUndated, titleVisibility: .visible) {
            Button("Send dated now") { sendToBeanconqueror(now: Date()) }
            Button("Cancel", role: .cancel) {}
        }
        .alert("Not saved", isPresented: Binding(get: { prepError != nil }, set: { if !$0 { prepError = nil } })) {
            Button("OK") { prepError = nil }
        } message: {
            Text(prepError ?? "")
        }
    }

    // MARK: Numbers

    private var numbers: some View {
        HStack(alignment: .firstTextBaseline, spacing: 22) {
            VStack(alignment: .leading, spacing: 0) {
                Text(BrewFormat.gramsUnit(shot.finalWeightG))
                    .font(.system(size: 28, weight: .semibold, design: .rounded)).monospacedDigit()
                    .foregroundStyle(brewPal.text)
                    .accessibilityIdentifier("lastShotWeight")
                Text("of \(BrewFormat.grams(stop))"
                     + (stop != shot.targetWeightG ? " (recipe \(BrewFormat.grams(shot.targetWeightG)))" : ""))
                    .font(.footnote).foregroundStyle(brewPal.dim)
            }
            VStack(alignment: .leading, spacing: 0) {
                Text(BrewFormat.secondsUnit(shot.timeBasis == "pour" ? shot.pourTimeS : shot.shotTimeS))
                    .font(.system(size: 28, weight: .semibold, design: .rounded)).monospacedDigit()
                    .foregroundStyle(brewPal.text)
                    .accessibilityIdentifier("lastShotTime")
                if shot.targetTimeS > 0 {
                    Text("of \(BrewFormat.decimal(shot.targetTimeS, 0))").font(.footnote).foregroundStyle(brewPal.dim)
                }
            }
            Spacer(minLength: 0)
        }
    }

    // MARK: Curve

    @ViewBuilder private var curve: some View {
        if let data = model.lastData, data.points.count > 1 {
            let points = data.points
            let weightMax = max(stop * 1.1, points.map(\.weight).max() ?? 1, model.refData?.weightG.max() ?? 0, 1)
            let flowMax = max(1, points.map(\.flow).max() ?? 1)
            let flowScale = weightMax / flowMax
            let refPoints = model.refData?.points ?? []
            Chart {
                RuleMark(y: .value("Stop", stop))
                    .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 6]))
                    .foregroundStyle(brewPal.dim)
                if shot.firstDropS > 0 {
                    RuleMark(x: .value("First drops", shot.firstDropS))
                        .lineStyle(StrokeStyle(lineWidth: 1, dash: [2, 4]))
                        .foregroundStyle(brewPal.dim.opacity(0.6))
                }
                ForEach(Array(points.enumerated()), id: \.offset) { _, point in
                    LineMark(x: .value("s", point.time), y: .value("g/s", point.flow * flowScale),
                             series: .value("Series", "flow"))
                        .foregroundStyle(brewPal.accent)
                }
                if refPoints.count > 1 {
                    ForEach(Array(refPoints.enumerated()), id: \.offset) { _, point in
                        LineMark(x: .value("s", point.time), y: .value("g", point.weight), series: .value("Series", "best"))
                            .foregroundStyle(brewPal.ok)
                            .lineStyle(StrokeStyle(lineWidth: 1.5, dash: [6, 5]))
                    }
                }
                ForEach(Array(points.enumerated()), id: \.offset) { _, point in
                    LineMark(x: .value("s", point.time), y: .value("g", point.weight), series: .value("Series", "weight"))
                        .foregroundStyle(brewPal.text)
                        .lineStyle(StrokeStyle(lineWidth: 2.5))
                }
            }
            .chartYScale(domain: 0...weightMax)
            .frame(height: 150)
            .accessibilityIdentifier("lastShotChart")
            Text(legend(peakFlow: flowMax))
                .font(.caption).foregroundStyle(brewPal.dim)
                .accessibilityIdentifier("lastShotLegend")
        } else if shot.hasData {
            Text("The curve is loading…").font(.footnote).foregroundStyle(brewPal.dim)
        } else {
            Text("The readings of this shot are no longer stored; the summary is.")
                .font(.footnote).foregroundStyle(brewPal.dim)
                .accessibilityIdentifier("lastShotNoCurve")
        }
    }

    private func legend(peakFlow: Double) -> String {
        var text = "White: weight. Orange: flow (peak \(BrewFormat.flowUnit(peakFlow))). Dotted: stop weight."
        if let ref = model.refShot, model.refData != nil {
            text += " Dashed green: your best-rated shot of this coffee (\(String(repeating: "★", count: ref.rating)), \(BrewFormat.secondsUnit(ref.shotTimeS)))."
        }
        return text
    }

    // MARK: Taste, stars, TDS

    private var tasteAndStars: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("How did it taste?").font(.footnote).foregroundStyle(brewPal.dim)
            Picker("Taste", selection: Binding(
                get: { tasteIndex },
                set: { newValue in Task { await model.rate(shotId: shot.id, rating: shot.rating, taste: newValue) } })) {
                ForEach(0..<Self.tasteTitles.count, id: \.self) { Text(Self.tasteTitles[$0]).tag($0) }
            }
            .pickerStyle(.segmented)
            .accessibilityIdentifier("tastePicker")
            HStack(spacing: 4) {
                ForEach(1...5, id: \.self) { n in
                    Button {
                        // The star already given is tapped again to clear it.
                        Task { await model.rate(shotId: shot.id, rating: shot.rating == n ? 0 : n, taste: tasteIndex) }
                    } label: {
                        Image(systemName: n <= shot.rating ? "star.fill" : "star")
                            .font(.title2).foregroundStyle(brewPal.accent)
                    }
                    .accessibilityLabel("\(n) star\(n > 1 ? "s" : "")")
                    .accessibilityIdentifier("star-\(n)")
                }
                if shot.rating == 0 { Text("tap to rate").font(.footnote).foregroundStyle(brewPal.dim) }
            }
        }
    }

    private var tdsRow: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                TextField("TDS %", text: $tdsText)
                    .keyboardType(.decimalPad)
                    .textFieldStyle(.roundedBorder)
                    .accessibilityIdentifier("tdsField")
                Button("Save TDS") {
                    let text = tdsText.replacingOccurrences(of: ",", with: ".").trimmingCharacters(in: .whitespaces)
                    let value = Double(text) ?? 0
                    if !text.isEmpty && !(value >= 0.5 && value <= 25) {
                        prepError = "TDS is a percent from 0.5 to 25"
                        return
                    }
                    Task { await model.setTDS(shotId: shot.id, tds: text.isEmpty ? 0 : value) }
                }
                .accessibilityIdentifier("tdsSave")
            }
            if let tds = shot.tdsPct, tds > 0 {
                Text(tdsLine(tds, shot.extractionPct)).font(.footnote).foregroundStyle(brewPal.dim)
            }
        }
    }

    private func tdsLine(_ tds: Double, _ extraction: Double?) -> String {
        var line = "TDS \(BrewFormat.decimal(tds, 2)) %"
        if let extraction, extraction > 0 { line += ", extraction \(BrewFormat.decimal(extraction, 1)) %" }
        return line
    }

    // MARK: Grind vote and preparation

    private var voteRow: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("For grind advice").font(.footnote).foregroundStyle(brewPal.dim)
            Picker("Grind vote", selection: Binding(
                get: { shot.grindVote == "count" ? 2 : shot.grindVote == "ignore" ? 1 : 0 },
                set: { newValue in Task { await model.setGrindVote(shotId: shot.id, vote: newValue) } })) {
                Text("Auto").tag(0)
                Text("Count").tag(2)
                Text("Ignore").tag(1)
            }
            .pickerStyle(.segmented)
            .accessibilityIdentifier("grindVotePicker")
        }
    }

    private var prepRow: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Preparation").font(.footnote).foregroundStyle(brewPal.dim)
            HStack {
                Text("dose weighed").font(.subheadline).foregroundStyle(brewPal.text)
                TextField(shot.doseG > 0 ? BrewFormat.grams(shot.doseG) : "g", text: $doseText)
                    .keyboardType(.decimalPad)
                    .textFieldStyle(.roundedBorder)
                    .frame(maxWidth: 90)
                    .accessibilityIdentifier("prepDose")
                Text("g").foregroundStyle(brewPal.dim)
            }
            Toggle("purged first", isOn: $purged).accessibilityIdentifier("prepPurged")
            Toggle("flow looked uneven", isOn: $uneven).accessibilityIdentifier("prepUneven")
            Button("Save preparation") { savePrep() }
                .buttonStyle(BrewKeyStyle(kind: .panel))
                .accessibilityIdentifier("prepSave")
        }
        .font(.subheadline)
        .foregroundStyle(brewPal.text)
    }

    private func savePrep() {
        let text = doseText.replacingOccurrences(of: ",", with: ".").trimmingCharacters(in: .whitespaces)
        let dose = text.isEmpty ? 0 : (Double(text) ?? -1)
        guard dose == 0 || (dose >= 5 && dose <= 40) else {
            prepError = "Dose weighed: 5 to 40 g, or leave it empty"
            return
        }
        Task { await model.setPrep(shot: shot, dose: dose, purged: purged, uneven: uneven) }
    }

    // MARK: Hand-offs

    private var actions: some View {
        VStack(spacing: 8) {
            Button("Send to Beanconqueror") {
                if shot.timestamp.isEmpty || shot.date == nil { askUndated = true } else { sendToBeanconqueror(now: nil) }
            }
            .buttonStyle(BrewKeyStyle(kind: .panel))
            .accessibilityIdentifier("lastShotBeanconqueror")
            if model.communityOn {
                Button("Share to Reddit") { BrewNavigator.shared.open(.reddit(shot.id)) }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .accessibilityIdentifier("lastShotReddit")
            }
        }
    }

    private func sendToBeanconqueror(now: Date?) {
        let shot = self.shot
        Task {
            let data = await model.data(of: shot)
            let library = model.library
            let coffee = library?.coffee(id: shot.coffeeId)
            let grind = shot.grindUm > 0 ? model.dial.setting(shot.grindUm) : ""
            guard let url = CoffeeBeanconqueror.link(shot: shot, data: data, coffee: coffee,
                                                     grinderModel: library?.grinder.model ?? "",
                                                     grindText: grind, now: now) else {
                model.errorMessage = "The shot could not be prepared for Beanconqueror."
                return
            }
            // Beanconqueror opens when it is installed (and has been opened once).
            openURL(url)
        }
    }

    // MARK: Sync

    /// Copies the shot's stored values into the fields (the fields are the owner's while they are typing:
    /// they only change when the shot changes).
    private var syncKey: String {
        "\(shot.id)|\(shot.tdsPct ?? -1)|\(shot.doseWeighedG)|\(shot.purged)|\(shot.uneven)"
    }

    private func sync() {
        tdsText = shot.tdsPct.map { BrewFormat.decimal($0, 2) } ?? ""
        doseText = shot.doseWeighedG > 0 ? BrewFormat.grams(shot.doseWeighedG) : ""
        purged = shot.purged
        uneven = shot.uneven
    }
}
