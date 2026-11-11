// The web page's Home below the board's panel: Getting started, machine power and its state pill, the
// grinder card, the most recent shot (LastShotCard.swift), the grinder advice given after it, For you
// and Community (CommunityCards.swift). Everything here reads routes the web page reads, through
// BrewModel. The cards follow the web page's panel switches (Setup, web page panels).
import SwiftUI

struct BrewHomeExtras: View {
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        VStack(spacing: 14) {
            if let power = model.power, power.paired { BrewPowerCard(power: power) }
            BrewGrinderCard()
            BrewLastShotCard()
            BrewPreviousAdviceCard()
            BrewForYouCard()
            if model.communityOn { BrewCommunityCard() }
        }
    }
}

// MARK: - Getting started

/// "Getting started: N of 7 done" with a bar and Continue (the web page's `hstart`), until every step is done.
struct BrewStartedCard: View {
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        let steps = model.startSteps
        let done = steps.filter { $0 }.count
        if model.startOn, !steps.isEmpty, done < steps.count {
            BrewCard {
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Text("Getting started").font(.headline).foregroundStyle(brewPal.text)
                        Text("\(done) of \(steps.count) done").font(.subheadline).foregroundStyle(brewPal.dim)
                            .accessibilityIdentifier("startedProgress")
                        Spacer()
                        Button("Continue") { BrewNavigator.shared.open(.started) }
                            .buttonStyle(.borderedProminent)
                            .tint(brewPal.accent)
                            .accessibilityIdentifier("startedContinue")
                    }
                    ProgressView(value: Double(done), total: Double(steps.count)).tint(brewPal.ok)
                }
            }
            .accessibilityIdentifier("startedCard")
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

/// The grinder's setting, the advice in a pill, why, the bean age and Details (the Grinder tab).
private struct BrewGrinderCard: View {
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        let card = LiveModel.shared.screen?.home.card
        let advice = model.grind.map { GrinderAdviceText.make($0, dial: model.dial) }
        BrewCard {
            VStack(alignment: .leading, spacing: 6) {
                HStack(alignment: .firstTextBaseline) {
                    Text("GRINDER").font(.subheadline).foregroundStyle(brewPal.dim)
                    Spacer()
                    Button("Details") { BrewNavigator.shared.select(.grinder) }
                        .font(.subheadline)
                        .accessibilityIdentifier("grinderDetails")
                }
                Text(setting(card)).font(.title3.weight(.semibold)).foregroundStyle(brewPal.text)
                    .accessibilityIdentifier("grinderSetting")
                if let advice {
                    Text(advice.title)
                        .font(.subheadline.weight(.semibold))
                        .padding(.horizontal, 10).padding(.vertical, 4)
                        .foregroundStyle(advice.tone == .plain ? brewPal.text : brewPal.bg)
                        .background(pill(advice.tone), in: Capsule())
                        .accessibilityIdentifier("grinderAdvicePill")
                    if !advice.why.isEmpty {
                        Text(advice.why).font(.footnote).foregroundStyle(brewPal.dim)
                    }
                } else if let text = card?.advice, !text.isEmpty {
                    Text(text).foregroundStyle(brewPal.color(forTone: card?.adviceTone))
                }
                if let grind = model.grind {
                    let age = GrinderAdviceText.beanAge(grind)
                    if !age.isEmpty { Text(age).font(.footnote).foregroundStyle(brewPal.dim) }
                } else if let age = card?.roastAge, !age.isEmpty {
                    Text(age).font(.footnote).foregroundStyle(brewPal.dim)
                }
            }
        }
        .accessibilityIdentifier("grinderCard")
    }

    private func setting(_ card: ScreenCard?) -> String {
        if let um = model.grind?.currentUm, um > 0 { return model.dial.settingUnit(um) }
        return card?.grind ?? "grind not set"
    }

    private func pill(_ tone: GrinderAdviceText.Tone) -> Color {
        switch tone {
        case .ok: return brewPal.ok
        case .warn: return brewPal.accent
        case .plain: return brewPal.track
        }
    }
}

// MARK: - The advice after the most recent shot

/// What the grinder advice was right after the most recent shot, as the web page words it.
private struct BrewPreviousAdviceCard: View {
    private var model: BrewModel { BrewModel.shared }

    private struct Words {
        var title: String
        var tone: GrinderAdviceText.Tone
    }

    private func words(_ prev: BrewPreviousAdvice) -> Words {
        let dial = model.dial
        let um = prev.adviceUm ?? 0
        let action = prev.adviceAction ?? ""
        switch action {
        case "hold": return Words(title: "Keep \(dial.settingUnit(um))", tone: .ok)
        case "finer", "coarser":
            let refined = prev.adviceRefined == true ? " (to your liked time)" : ""
            return Words(title: "Go \(action) \(dial.delta(prev.adviceDeltaUm ?? 0)) \(dial.deltaUnit), to \(dial.settingUnit(um))" + refined,
                         tone: .warn)
        case "inconsistent": return Words(title: "Brew times vary: no advice", tone: .warn)
        case "no_data": return Words(title: "No advice: the shot did not count", tone: .plain)
        case "no_grind_set": return Words(title: "No advice: no grind setting entered", tone: .plain)
        default: return Words(title: "No advice", tone: .plain)
        }
    }

    /// "After X, recipe, when, pulled at Y. From N shots at that setting. Limited ... The grinder is at Z now."
    private func sentence(_ shot: ShotModel, _ prev: BrewPreviousAdvice) -> String {
        let dial = model.dial
        let coffee = shot.coffee.isEmpty ? "no coffee" : shot.coffee
        let pulled = shot.grindUm > 0 ? dial.settingUnit(shot.grindUm) : "no setting recorded"
        var text = "After \(coffee), \(shot.recipe), \(BrewShotWords.when(shot)), pulled at \(pulled)."
        if let count = prev.adviceShots, count > 0 { text += " From \(count) shot\(count == 1 ? "" : "s") at that setting." }
        let moves = prev.adviceAction == "finer" || prev.adviceAction == "coarser"
        if moves, prev.adviceStepLimited == true { text += " Limited to the largest single change." }
        if let preset = model.activePreset, let current = model.grind?.currentUm, current > 0,
           shot.coffeeId == preset.coffeeId, shot.recipeId == preset.recipeId, current != shot.grindUm {
            text += " The grinder is at \(dial.settingUnit(current)) now."
        }
        return text
    }

    private func fill(_ tone: GrinderAdviceText.Tone) -> Color {
        switch tone {
        case .ok: return brewPal.ok
        case .warn: return brewPal.accent
        case .plain: return brewPal.track
        }
    }

    var body: some View {
        BrewCard {
            VStack(alignment: .leading, spacing: 6) {
                HStack(alignment: .firstTextBaseline) {
                    Text("PREVIOUS ADVICE").font(.subheadline).foregroundStyle(brewPal.dim)
                    Spacer()
                    Button("All shots") { BrewNavigator.shared.select(.shots) }
                        .font(.subheadline)
                        .accessibilityIdentifier("previousAllShots")
                }
                if let shot = model.lastShot {
                    if let prev = model.previous, prev.shotId == shot.id {
                        let w = words(prev)
                        Text(w.title)
                            .font(.subheadline.weight(.semibold))
                            .padding(.horizontal, 10).padding(.vertical, 4)
                            .foregroundStyle(w.tone == .plain ? brewPal.text : brewPal.bg)
                            .background(fill(w.tone), in: Capsule())
                            .accessibilityIdentifier("previousAdvicePill")
                        Text(sentence(shot, prev)).font(.footnote).foregroundStyle(brewPal.text)
                            .accessibilityIdentifier("previousAdviceText")
                    } else {
                        Text("No grinder recommendation was recorded for the most recent shot.")
                            .foregroundStyle(brewPal.dim)
                    }
                } else {
                    Text("No shot yet. The advice given after your next shot appears here.")
                        .foregroundStyle(brewPal.dim)
                }
            }
        }
        .accessibilityIdentifier("previousAdviceCard")
    }
}
