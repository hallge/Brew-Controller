// The words for the grind advice (the web page's drawGrind), from the fields Core's GrindAdviceModel has,
// and the parser for a setting typed the way the dial reads. Pure functions: unit-testable.
import Foundation

/// The pill and the explanation under it.
struct GrinderAdviceText: Equatable {
    enum Tone { case ok, warn, plain }
    var title: String
    var tone: Tone
    var why: String
    /// The "I set the grinder to ..." button, when the advice is a move.
    var applyLabel: String?
    var applyUm: Int?

    /// `dial` is how the owner's grinder writes a setting (`GrinderModel.dial`).
    static func make(_ g: GrindAdviceModel, dial: BrewFormat.Dial) -> GrinderAdviceText {
        func setting(_ um: Int) -> String { dial.settingUnit(um) }
        let basis = "Brew time \(g.timeBasis == "pour" ? "(pour) " : "")averaged over the last \(g.shotsUsed) shot\(g.shotsUsed == 1 ? "" : "s") at \(setting(g.currentUm)): \(BrewFormat.seconds(g.averageTimeS)) s against a target of \(BrewFormat.seconds(g.targetTimeS)) s ± \(BrewFormat.seconds(g.toleranceS)) s."
        switch g.action {
        case "no_grind_set":
            let text = "Shots are stored with the grind setting entered here. Advice starts with the first valid shot."
            return GrinderAdviceText(title: "Enter the grinder’s setting", tone: .plain, why: text)
        case _ where g.checkPrep:
            let why: String
            switch g.checkPrepWhy {
            case "too_short":
                why = "The last shot at \(setting(g.currentUm)) ran under 15 s, which is usually the puck (channelling, dose, tamp), not the grind."
            case "uneven":
                why = "You marked the flow of the last shot at \(setting(g.currentUm)) uneven. Check dose, distribution and tamp; the grind is kept."
            default:
                why = "The last shot at \(setting(g.currentUm)) ran well fast and tasted sour. That is as likely a channelled puck as a coarse grind."
            }
            return GrinderAdviceText(title: "Check the preparation first", tone: .warn,
                                     why: why + " Check the preparation and pull another at \(setting(g.currentUm)). Grinding finer would make a channelling puck worse, so no change is suggested until a shot comes out evenly.")
        case "no_data":
            return GrinderAdviceText(title: "No shot at this setting yet", tone: .plain,
                                     why: "Pull a shot at \(setting(g.currentUm)). It counts when it stops on the weight and runs from 15 s to the target time + 15 s.")
        case "inconsistent":
            return GrinderAdviceText(title: "Brew times vary: no advice", tone: .warn,
                                     why: "The last \(g.shotsUsed) shots at \(setting(g.currentUm)) ran some fast and some slow against \(BrewFormat.seconds(g.targetTimeS)) s ± \(BrewFormat.seconds(g.toleranceS)) s. Check dose, distribution and tamp before moving the grind.")
        case "hold":
            var title = "Keep \(setting(g.currentUm))"
            var tone = Tone.ok
            var why = basis
            if g.betweenSteps {
                title += " (closest step)"
                why += " The target lies between this setting and the next one, which ran on the other side of it; there is no setting between them, so the closer one is kept."
            } else if g.atLimit {
                title += " (end of the range)"
                tone = .warn
                why += " The shots ask for a change past the end of this grinder’s range, so there is nothing left to turn: try a different dose, or a fresher or older coffee."
            } else {
                why += " Inside tolerance."
            }
            return GrinderAdviceText(title: title, tone: tone, why: why)
        case "finer", "coarser":
            let finer = g.action == "finer"
            let title = "Go \(finer ? "finer" : "coarser") \(dial.delta(g.deltaUm)) \(dial.deltaUnit), to \(setting(g.suggestedUm))\(g.refined ? " (to your liked time)" : "")"
            var why = g.outOfRange
                ? "The last shot ran \(finer ? "too short (under 15 s)" : "more than 15 s over the target") to be counted, so its time cannot size the change. The direction is clear: this is the largest single change. The next shot gets the usual advice."
                : basis + " Running \(BrewFormat.seconds(abs(g.errorS))) s \(finer ? "fast" : "slow")."
            if !g.outOfRange {
                why += " Sensitivity \(BrewFormat.decimal(g.umPerS, 2)) \(dial.unit) per second"
                    + (g.sensitivitySource == "measured" ? ", measured from \(g.learnShots) of your shots." : g.sensitivitySource == "grinder" ? ", from what this grinder has shown." : " is the starting guess.")
            }
            return GrinderAdviceText(title: title, tone: .warn, why: why,
                                     applyLabel: "I set the grinder to \(dial.setting(g.suggestedUm))", applyUm: g.suggestedUm)
        default:
            return GrinderAdviceText(title: g.action.isEmpty ? "No advice yet" : g.action, tone: .plain, why: "")
        }
    }

    /// The bean-age line.
    static func beanAge(_ g: GrindAdviceModel) -> String {
        guard !g.coffee.isEmpty else { return "" }
        if g.beanAgeDays < 0 { return "Bean age: no roast date is entered for this bag, so its age cannot be taken into account." }
        var text = "Bean age: this bag is \(g.beanAgeDays) day\(g.beanAgeDays == 1 ? "" : "s") off roast."
        if g.beanAgeDays < 5 {
            text += " Very fresh coffee is still giving off gas: expect shots to vary for a few days, and settings found now to need revisiting."
        }
        return text
    }
}

/// Reads a setting the way the dial writes it: "246", "3.5" (divisor 2 to 100), "12:3" (two rings).
enum GrinderSettingParser {
    /// The stored whole steps, or nil when the text is not a setting of this dial. 1...2000.
    static func parse(_ text: String, dial: BrewFormat.Dial) -> Int? {
        let clean = text.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
        guard !clean.isEmpty else { return nil }
        let stored: Int
        if dial.isRinged {
            let parts = clean.split(separator: ":", omittingEmptySubsequences: false)
            guard parts.count == 2, let outer = Int(parts[0]), let inner = Int(parts[1]),
                  outer >= 1, inner >= 1, inner <= dial.subSteps else { return nil }
            stored = (outer - 1) * dial.subSteps + inner
        } else if dial.divisor > 1 {
            guard let value = Double(clean), value >= 0 else { return nil }
            stored = Int((value * Double(dial.divisor)).rounded())
        } else {
            guard let value = Int(clean) else { return nil }
            stored = value
        }
        return (1...2000).contains(stored) ? stored : nil
    }

    /// The hint under the field when the text is not a setting.
    static func hint(dial: BrewFormat.Dial) -> String {
        if dial.isRinged { return "outer ring : inner ring, for example 12:3" }
        if dial.divisor > 1 { return "a number such as 3.5" }
        return "a whole number"
    }
}
