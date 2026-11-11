// The board's EDIT RECIPE panel: TARGET (or RATIO), DOSE, GRIND, BREW TIME and PRE-INF, with - and +
// keys, SET (the advised grind), SAVE and CANCEL. The steps are the board's (Ui.cpp editStep) and the
// field wording is a copy of ui/ScreenText.h describeEditField (that text is not served by
// /api/screen). SAVE is POST /api/screen/do?what=apply_preset_edit.
import SwiftUI

/// The values on the edit screen. Works on a copy; SAVE sends it.
struct BrewRecipeDraft: Equatable, Sendable {
    var useRatio = false
    var ratio: Double = 2
    var targetG: Double = 36
    var doseG: Double = 18
    /// The grinder's stored setting; 0: not set.
    var grindUm = 0
    var targetTimeS: Double = 28
    var hold = false
    var holdMs = 500
    /// A recipe with a bloom keeps its pre-infusion hold.
    var hasBloom = false
    /// The grinder's range in stored steps (0: not given).
    var rangeMin = 0
    var rangeMax = 0

    static let fieldTitles = ["TARGET", "DOSE", "GRIND", "BREW TIME", "PRE-INF"]
    static let grindField = 2

    init() {}

    init(preset: PresetModel, recipe: RecipeDefModel, grinder: GrinderModel) {
        useRatio = recipe.useRatio
        ratio = recipe.ratio
        targetG = recipe.targetWeightG
        doseG = recipe.doseG
        grindUm = preset.grindUm
        targetTimeS = recipe.targetTimeS
        hold = recipe.preinfusion == 1
        holdMs = max(300, Int((recipe.preinfusionHoldS * 1000).rounded()))
        hasBloom = recipe.bloomPauseS > 0
        rangeMin = grinder.min
        rangeMax = grinder.max
    }

    private static func tenth(_ value: Double) -> Double { (value * 10).rounded() / 10 }

    /// One tap (or one long press) on - or +, as the board steps it.
    mutating func step(field: Int, direction: Int, held: Bool) {
        let d = Double(direction)
        switch field {
        case 0:
            if useRatio { ratio = Self.tenth(ratio + d * 0.1) } else { targetG = Self.tenth(targetG + d * (held ? 0.1 : 0.5)) }
        case 1:
            doseG = Self.tenth(doseG + d * (held ? 0.5 : 0.1))
        case 2:
            // Somewhere to start from on first use: the middle of the grinder's range, else 250.
            var um = grindUm != 0 ? grindUm : (rangeMax > rangeMin ? (rangeMin + rangeMax) / 2 : 250)
            um += direction * (held ? 10 : 1)
            if rangeMax > rangeMin { um = min(max(um, rangeMin), rangeMax) }
            grindUm = max(1, min(2000, um))
        case 3:
            targetTimeS = Self.tenth(targetTimeS + d * (held ? 1.0 : 0.5))
        default:
            // Minus at the shortest hold goes back to the machine's own pre-infusion.
            if !hold {
                if direction > 0 { hold = true; holdMs = 500 }
            } else {
                let ms = holdMs + direction * (held ? 500 : 100)
                if ms < 300 {
                    if hasBloom { holdMs = 300 } else { hold = false }
                } else {
                    holdMs = min(ms, 30_000)
                }
            }
        }
        clamp()
    }

    mutating func clamp() {
        doseG = min(max(doseG, 5), 30)
        targetG = min(max(targetG, 5), 200)
        ratio = min(max(ratio, 1), 5)
        targetTimeS = min(max(targetTimeS, 5), 60)
    }

    /// The arguments of apply_preset_edit.
    func arguments(index: Int) -> [String: String] {
        var args: [String: String] = [
            "index": String(index),
            "dose_g": BrewFormat.decimal(doseG, 1),
            "target_g": BrewFormat.decimal(targetG, 1),
            "ratio": BrewFormat.decimal(ratio, 1),
            "use_ratio": useRatio ? "true" : "false",
            "target_time_s": BrewFormat.decimal(targetTimeS, 1),
            "preinfusion": hold ? "hold" : "machine",
        ]
        if grindUm > 0 { args["grind_um"] = String(grindUm) }
        if hold { args["preinfusion_hold_ms"] = String(holdMs) }
        return args
    }

    /// The field's title, value and hint (a copy of describeEditField).
    func describe(field: Int, dial: BrewFormat.Dial, advice: GrindAdviceModel?) -> (title: String, value: String, hint: String) {
        switch field {
        case 0:
            if useRatio {
                return ("RATIO", BrewFormat.ratio(ratio),
                        "\(BrewFormat.decimal(doseG, 1)) g in gives\n\(BrewFormat.decimal(doseG * ratio, 1)) g out")
            }
            return ("TARGET OUT", BrewFormat.gramsUnit(targetG), "tap 0.5 g,\nhold for 0.1 g")
        case 1:
            return ("DOSE IN", BrewFormat.gramsUnit(doseG), "dry coffee in\nthe basket")
        case 2:
            let value = grindUm > 0 ? dial.settingUnit(grindUm) : "not set"
            var hint = "number on the\ngrinder: tap 1,\nhold for 10"
            if let advice, advice.action == "finer" || advice.action == "coarser" {
                let way = advice.action
                hint = advice.hasNumber
                    ? "advice: go \(way),\nset \(dial.settingUnit(advice.suggestedUm))"
                    : "advice: go \(way)\nhow far: see the\nweb page"
            }
            return ("GRIND", value, hint)
        case 3:
            return ("TARGET TIME", BrewFormat.secondsUnit(targetTimeS), "brew time the\ngrind advice\naims for")
        default:
            if hold {
                return ("PRE-INFUSION", "HOLD \(BrewFormat.decimal(Double(holdMs) / 1000, 1)) s",
                        "pre-infusion lasts\nonly as long as\nthe hold")
            }
            return ("PRE-INFUSION", "MACHINE", "press and release:\nthe machine\npre-infuses")
        }
    }

    /// The setting the grind advice names, when it names one.
    static func advisedSetting(_ advice: GrindAdviceModel?) -> Int? {
        guard let advice, advice.action == "finer" || advice.action == "coarser", advice.hasNumber,
              advice.suggestedUm > 0 else { return nil }
        return advice.suggestedUm
    }
}

struct BrewEditRecipeView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var draft: BrewRecipeDraft
    @State private var field: Int
    @State private var saving = false
    private let index: Int
    private var model: BrewModel { BrewModel.shared }

    init(index: Int, draft: BrewRecipeDraft, field: Int = BrewRecipeDraft.grindField) {
        self.index = index
        _draft = State(initialValue: draft)
        _field = State(initialValue: field)
    }

    var body: some View {
        let text = draft.describe(field: field, dial: model.dial, advice: model.grind)
        let advised = BrewRecipeDraft.advisedSetting(model.grind)
        ZStack {
            brewPal.bg.ignoresSafeArea()
            ScrollView {
                VStack(spacing: 14) {
                    HStack(spacing: 8) {
                        ForEach(0..<BrewRecipeDraft.fieldTitles.count, id: \.self) { i in
                            Button {
                                field = i
                            } label: {
                                Text(i == 0 && draft.useRatio ? "RATIO" : BrewRecipeDraft.fieldTitles[i])
                                    .font(.caption.weight(.semibold))
                                    .lineLimit(1)
                                    .minimumScaleFactor(0.6)
                            }
                            .buttonStyle(BrewFieldKeyStyle(selected: i == field))
                            .accessibilityIdentifier("editField-\(i)")
                        }
                    }
                    BrewCard {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(text.title).font(.subheadline).foregroundStyle(brewPal.dim)
                                .accessibilityIdentifier("editTitle")
                            Text(text.value).font(.system(size: 40, weight: .semibold, design: .rounded))
                                .foregroundStyle(brewPal.text)
                                .minimumScaleFactor(0.5).lineLimit(2)
                                .accessibilityIdentifier("editValue")
                            Text(text.hint).font(.subheadline).foregroundStyle(brewPal.dim)
                                .accessibilityIdentifier("editHint")
                        }
                    }
                    HStack(spacing: 12) {
                        stepKey("minus", -1)
                        stepKey("plus", +1)
                    }
                    if field == BrewRecipeDraft.grindField, let advised, advised != draft.grindUm {
                        Button("SET \(model.dial.settingUnit(advised))") { draft.grindUm = advised }
                            .buttonStyle(BrewKeyStyle(kind: .accent))
                            .accessibilityIdentifier("editSetAdvice")
                    }
                    Button {
                        saving = true
                        Task {
                            let saved = await model.saveEdit(draft, index: index)
                            saving = false
                            if saved { dismiss() }
                        }
                    } label: { Text("SAVE") }
                        .buttonStyle(BrewKeyStyle(kind: .ok))
                        .disabled(saving)
                        .accessibilityIdentifier("editSave")
                    Button { dismiss() } label: { Text("CANCEL") }
                        .buttonStyle(BrewKeyStyle(kind: .panel))
                        .accessibilityIdentifier("editCancel")
                }
                .padding(16)
            }
        }
        .accessibilityIdentifier("editPanel")
        .alert("Not saved", isPresented: Binding(get: { model.errorMessage != nil }, set: { if !$0 { model.errorMessage = nil } })) {
            Button("OK") { model.errorMessage = nil }
        } message: {
            Text(model.errorMessage ?? "")
        }
    }

    /// A - or + key: a tap steps by the small amount, a long press by the large one (the board's hold).
    private func stepKey(_ name: String, _ direction: Int) -> some View {
        Image(systemName: name)
            .font(.title.weight(.bold))
            .foregroundStyle(brewPal.text)
            .frame(maxWidth: .infinity, minHeight: 72)
            .background(brewPal.panel, in: RoundedRectangle(cornerRadius: 14))
            .contentShape(RoundedRectangle(cornerRadius: 14))
            .onTapGesture { draft.step(field: field, direction: direction, held: false) }
            .onLongPressGesture(minimumDuration: 0.5) { draft.step(field: field, direction: direction, held: true) }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(direction < 0 ? "Less" : "More")
            .accessibilityAddTraits(.isButton)
            .accessibilityIdentifier("edit\(name == "minus" ? "Minus" : "Plus")")
            .accessibilityAction { draft.step(field: field, direction: direction, held: false) }
    }
}

/// One of the field keys across the top of the edit panel; the open one is highlighted.
private struct BrewFieldKeyStyle: ButtonStyle {
    var selected: Bool

    @MainActor
    func makeBody(configuration: Configuration) -> some View {
        let p = BrewTheme.shared.palette
        return configuration.label
            .foregroundStyle(p.text)
            .frame(maxWidth: .infinity, minHeight: 44)
            .background(selected ? p.select : p.panel, in: RoundedRectangle(cornerRadius: 10))
            .opacity(configuration.isPressed ? 0.7 : 1)
    }
}
