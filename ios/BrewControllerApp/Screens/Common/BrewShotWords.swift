// How the Home cards word a shot: when it was pulled, how far it was from the target, what ended it.
// Ports of the web page's `when`, `stopT` and the "Last shot on target / over / under" line.
import Foundation

enum BrewShotWords {
    /// "Nov 11, 9:42 AM", or "#id" while the controller's clock was not set.
    static func when(_ shot: ShotModel) -> String {
        guard let date = shot.date else { return "#\(shot.id)" }
        return date.formatted(.dateTime.month(.abbreviated).day().hour().minute())
    }

    /// The weight the shot was stopped for: the stop target, else the recipe's.
    static func stopTarget(_ shot: ShotModel) -> Double {
        shot.stopTargetG > 0 ? shot.stopTargetG : shot.targetWeightG
    }

    /// "Last shot on target", "Last shot 1.4 g over, did not settle".
    static func verdict(_ shot: ShotModel) -> String {
        let delta = shot.finalWeightG - stopTarget(shot)
        let how = abs(delta) < 0.05 ? "on target" : "\(BrewFormat.grams(abs(delta))) g \(delta > 0 ? "over" : "under")"
        return "Last shot " + how + (shot.finalValid ? "" : ", did not settle")
    }

    /// The line under the numbers: coffee, recipe, first drops, pour, bloom, when.
    static func detail(_ shot: ShotModel) -> String {
        var text = (shot.coffee.isEmpty ? "" : shot.coffee + ", ") + shot.recipe + ". "
        text += "First drops \(BrewFormat.seconds(shot.firstDropS)) s. "
        text += "Pour \(BrewFormat.seconds(shot.pourTimeS)) s at \(BrewFormat.flowUnit(shot.pourFlowGS))"
        if shot.bloomPauseS > 0 { text += ". Bloom \(BrewFormat.decimal(shot.bloomPauseS, 0)) s" }
        return text + ". " + when(shot) + "."
    }
}
