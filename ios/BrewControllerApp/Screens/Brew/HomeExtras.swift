// The web page's Home below the board's panel: machine power and its state pill, the grinder card, the
// grinder recommendation given after the most recent shot, the most recent shot (chart, stars, taste,
// TDS), For you and Community. Everything here reads routes the web page reads, through BrewModel.
import Charts
import SwiftUI

struct BrewHomeExtras: View {
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        VStack(spacing: 14) {
            if let power = model.power, power.paired { BrewPowerCard(power: power) }
            BrewGrinderCard()
            BrewPreviousAdviceCard()
            if let shot = model.lastShot { BrewLastShotCard(data: shot) }
            if let reco = model.forYou { BrewForYouCard(reco: reco) }
            if let community = model.community, community.found == true { BrewCommunityCard(info: community) }
        }
    }
}

// MARK: - Machine power

private struct BrewPowerCard: View {
    var power: PowerModel
    private var model: BrewModel { BrewModel.shared }

    private var belief: String {
        power.belief == "on" ? "On" : power.belief == "off" ? "Off" : "Not known"
    }

    var body: some View {
        BrewCard {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Text("MACHINE").font(.subheadline).foregroundStyle(brewPal.dim)
                    Spacer()
                    // The pill says whether the machine is on; tapping it only changes what the app believes.
                    Button {
                        Task { await model.believePower(on: power.belief != "on") }
                    } label: {
                        Text(belief)
                            .font(.subheadline.weight(.semibold))
                            .padding(.horizontal, 12).padding(.vertical, 6)
                            .background(power.belief == "on" ? brewPal.ok : brewPal.track, in: Capsule())
                            .foregroundStyle(power.belief == "on" ? brewPal.bg : brewPal.text)
                    }
                    .accessibilityIdentifier("machineStatePill")
                }
                Button(power.working ? "PRESSING…" : "POWER") { Task { await model.pressPower() } }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .disabled(power.working)
                    .accessibilityIdentifier("powerButton")
                if !power.last.isEmpty {
                    Text("Last: \(power.last)\(power.lastBySchedule ? " (schedule)" : "")")
                        .font(.footnote).foregroundStyle(brewPal.dim)
                }
            }
        }
        .accessibilityIdentifier("powerCard")
    }
}

// MARK: - Grinder

private struct BrewGrinderCard: View {
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        let card = LiveModel.shared.screen?.home.card
        BrewCard {
            VStack(alignment: .leading, spacing: 6) {
                Text("GRINDER").font(.subheadline).foregroundStyle(brewPal.dim)
                Text(card?.grind ?? "").font(.title3.weight(.semibold)).foregroundStyle(brewPal.text)
                if let advice = card?.advice, !advice.isEmpty {
                    Text(advice).foregroundStyle(brewPal.color(forTone: card?.adviceTone))
                }
                if let grind = model.grind, grind.checkPrep, !grind.checkPrepWhy.isEmpty {
                    Text(grind.checkPrepWhy).font(.footnote).foregroundStyle(brewPal.dim)
                }
                if let age = card?.roastAge, !age.isEmpty {
                    Text(age).font(.footnote).foregroundStyle(brewPal.dim)
                }
            }
        }
        .accessibilityIdentifier("grinderCard")
    }
}

/// What the grinder advice was right after the most recent shot, as the web page words it.
private struct BrewPreviousAdviceCard: View {
    private var model: BrewModel { BrewModel.shared }

    private var title: String? {
        guard let shot = model.lastShot?.shot, let prev = model.previous, prev.shotId == shot.id else { return nil }
        let dial = model.dial
        let um = prev.adviceUm ?? 0
        switch prev.adviceAction ?? "" {
        case "hold": return "Keep \(dial.settingUnit(um))"
        case "finer", "coarser":
            let way = prev.adviceAction ?? ""
            return "Go \(way) \(dial.delta(prev.adviceDeltaUm ?? 0)) \(dial.deltaUnit), to \(dial.settingUnit(um))"
                + (prev.adviceRefined == true ? " (to your liked time)" : "")
        case "inconsistent": return "Brew times vary: no advice"
        case "no_data": return "No advice: the shot did not count"
        case "no_grind_set": return "No advice: no grind setting entered"
        default: return "No advice"
        }
    }

    var body: some View {
        if model.lastShot != nil {
            BrewCard {
                VStack(alignment: .leading, spacing: 4) {
                    Text("PREVIOUS ADVICE").font(.subheadline).foregroundStyle(brewPal.dim)
                    Text(title ?? "No grinder recommendation was recorded for the most recent shot.")
                        .foregroundStyle(title == nil ? brewPal.dim : brewPal.text)
                }
            }
            .accessibilityIdentifier("previousAdviceCard")
        }
    }
}

// MARK: - Most recent shot

private struct BrewLastShotCard: View {
    var data: ShotDataModel
    private var model: BrewModel { BrewModel.shared }
    @State private var tdsText = ""

    private static let tastes = ["", "sour", "balanced", "bitter"]
    private static let tasteTitles = ["—", "Sour", "Balanced", "Bitter"]

    var body: some View {
        let shot = data.shot
        BrewCard {
            VStack(alignment: .leading, spacing: 10) {
                Text("MOST RECENT SHOT").font(.subheadline).foregroundStyle(brewPal.dim)
                Text(summary(shot)).foregroundStyle(brewPal.text).accessibilityIdentifier("lastShotSummary")
                if data.points.count > 1 {
                    Chart {
                        ForEach(Array(data.points.enumerated()), id: \.offset) { _, point in
                            LineMark(x: .value("s", point.time), y: .value("g", point.weight))
                                .foregroundStyle(brewPal.ok)
                        }
                    }
                    .frame(height: 120)
                    .accessibilityIdentifier("lastShotChart")
                }
                Text("Average flow \(BrewFormat.flowUnit(shot.averageFlowGS))")
                    .font(.footnote).foregroundStyle(brewPal.dim)
                stars(shot)
                Picker("Taste", selection: Binding(
                    get: { Self.tastes.firstIndex(of: shot.taste) ?? 0 },
                    set: { newValue in Task { await model.rate(shotId: shot.id, rating: shot.rating, taste: newValue) } })) {
                    ForEach(0..<Self.tasteTitles.count, id: \.self) { Text(Self.tasteTitles[$0]).tag($0) }
                }
                .pickerStyle(.segmented)
                .accessibilityIdentifier("tastePicker")
                HStack {
                    TextField("TDS %", text: $tdsText)
                        .keyboardType(.decimalPad)
                        .textFieldStyle(.roundedBorder)
                        .accessibilityIdentifier("tdsField")
                    Button("Save TDS") {
                        let value = Double(tdsText.replacingOccurrences(of: ",", with: ".")) ?? 0
                        Task { await model.setTDS(shotId: shot.id, tds: value) }
                    }
                    .accessibilityIdentifier("tdsSave")
                }
                if let tds = shot.tdsPct {
                    Text(tdsLine(tds, shot.extractionPct)).font(.footnote).foregroundStyle(brewPal.dim)
                }
            }
        }
        .accessibilityIdentifier("lastShotCard")
        .onAppear { if let tds = data.shot.tdsPct { tdsText = BrewFormat.decimal(tds, 2) } }
    }

    private func tdsLine(_ tds: Double, _ extraction: Double?) -> String {
        var line = "TDS \(BrewFormat.decimal(tds, 2)) %"
        if let extraction { line += ", extraction \(BrewFormat.decimal(extraction, 1)) %" }
        return line
    }

    private func summary(_ shot: ShotModel) -> String {
        let name = shot.coffee.isEmpty ? shot.recipe : "\(shot.coffee), \(shot.recipe)"
        let time = shot.timeBasis == "pour" ? shot.pourTimeS : shot.shotTimeS
        return "\(name): \(BrewFormat.gramsUnit(shot.finalWeightG)) in \(BrewFormat.secondsUnit(time))"
    }

    private func stars(_ shot: ShotModel) -> some View {
        HStack(spacing: 4) {
            ForEach(1...5, id: \.self) { n in
                Button {
                    // The star already given is tapped again to clear it.
                    Task { await model.rate(shotId: shot.id, rating: shot.rating == n ? 0 : n, taste: Self.tastes.firstIndex(of: shot.taste) ?? 0) }
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

// MARK: - For you, Community

private struct BrewForYouCard: View {
    var reco: BrewTasteRecommendation

    var body: some View {
        BrewCard {
            VStack(alignment: .leading, spacing: 8) {
                Text("FOR YOU").font(.subheadline).foregroundStyle(brewPal.dim)
                if reco.shown != true {
                    Text("Rate \(max(1, 3 - (reco.coffees ?? 0))) more coffee\(max(1, 3 - (reco.coffees ?? 0)) == 1 ? "" : "s") on the Coffees tab and recommendations appear here.")
                        .font(.footnote).foregroundStyle(brewPal.dim)
                } else if let items = reco.items, !items.isEmpty {
                    ScrollView(.horizontal, showsIndicators: false) {
                        HStack(spacing: 8) {
                            ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(item.name ?? "").font(.subheadline.weight(.semibold)).foregroundStyle(brewPal.text)
                                    Text([item.roaster, item.origin].compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: " · "))
                                        .font(.caption).foregroundStyle(brewPal.dim)
                                }
                                .padding(10)
                                .background(brewPal.track, in: RoundedRectangle(cornerRadius: 10))
                            }
                        }
                    }
                } else {
                    Text("Nothing new to recommend yet.").font(.footnote).foregroundStyle(brewPal.dim)
                }
            }
        }
        .accessibilityIdentifier("forYouCard")
    }
}

private struct BrewCommunityCard: View {
    var info: BrewCommunityInfo
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        BrewCard {
            VStack(alignment: .leading, spacing: 6) {
                Text("COMMUNITY").font(.subheadline).foregroundStyle(brewPal.dim)
                Text("\(info.contributors ?? 0) people, \(info.dialins ?? 0) dial-ins")
                    .foregroundStyle(brewPal.text)
                if let time = info.timeS, time > 0 {
                    Text("Usual brew time \(BrewFormat.secondsUnit(time))").foregroundStyle(brewPal.text)
                }
                if let dose = info.theirDoseG, let out = info.theirYieldG, dose > 0 {
                    Text("Their recipe \(BrewFormat.decimal(dose, 1)) g in, \(BrewFormat.decimal(out, 1)) g out")
                        .foregroundStyle(brewPal.text)
                }
                if let setting = info.start?.setting, setting > 0 {
                    Text("Start at \(model.dial.settingUnit(setting))").foregroundStyle(brewPal.accent)
                }
            }
            .font(.subheadline)
        }
        .accessibilityIdentifier("communityCard")
    }
}
