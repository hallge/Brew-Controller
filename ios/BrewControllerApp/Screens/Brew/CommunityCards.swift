// Home's "For you" chips and the Community card ("for what you are brewing"), as the web page words
// them: loadForYou, drawCommunity and simHtml. The chips lead on to Discover, the Coffees tab and the
// Grinder tab; the Community card shows every state the web page has (no coffee, not in the shared
// library, nobody shared, unreachable, found) and offers the starting grind.
import SwiftUI

// MARK: - For you

/// Chips for what there is to discover: coffees and subscriptions that match the taste, and the next
/// grind move. Hidden when there is nothing.
struct BrewForYouCard: View {
    private var model: BrewModel { BrewModel.shared }

    private struct Chip: Identifiable {
        var id: String
        var title: String
        var bold: String
        var action: () -> Void
    }

    private var chips: [Chip] {
        var list: [Chip] = []
        if let taste = model.forYou {
            if taste.shown == true, let items = taste.items, let first = items.first {
                list.append(Chip(id: "taste", title: "Coffees you might like: ",
                                 bold: (first.name ?? "") + (items.count > 1 ? " and \(items.count - 1) more" : ""),
                                 action: { BrewNavigator.shared.open(.discover) }))
            } else if taste.shown != true {
                let missing = max(1, 3 - (taste.coffees ?? 0))
                list.append(Chip(id: "rate", title: "Rate ", bold: "\(missing) more coffee\(missing == 1 ? "" : "s")",
                                 action: { BrewNavigator.shared.select(.coffees) }))
            }
        }
        if let name = model.subscriptionReco?.items?.first?.service?.name, !name.isEmpty {
            list.append(Chip(id: "subs", title: "Subscriptions you might like: ", bold: name,
                             action: { BrewNavigator.shared.open(.discover) }))
        }
        if let grind = model.grind, grind.hasNumber, grind.action == "finer" || grind.action == "coarser", grind.suggestedUm > 0 {
            let dial = model.dial
            list.append(Chip(id: "grind", title: "Next shot: ",
                             bold: "\(grind.action) \(dial.delta(grind.deltaUm)) \(dial.deltaUnit), to \(dial.setting(grind.suggestedUm))",
                             action: { BrewNavigator.shared.select(.grinder) }))
        }
        return list
    }

    var body: some View {
        let chips = self.chips
        if !chips.isEmpty {
            BrewCard {
                VStack(alignment: .leading, spacing: 8) {
                    Text("FOR YOU").font(.subheadline).foregroundStyle(brewPal.dim)
                    ForEach(chips) { chip in
                        Button(action: chip.action) {
                            HStack {
                                (Text(chip.title).foregroundColor(brewPal.text)
                                    + Text(chip.bold).bold().foregroundColor(brewPal.accent))
                                    .multilineTextAlignment(.leading)
                                Spacer(minLength: 4)
                                Image(systemName: "chevron.right").font(.footnote).foregroundStyle(brewPal.dim)
                            }
                            .padding(10)
                            .background(brewPal.track, in: RoundedRectangle(cornerRadius: 10))
                            .contentShape(RoundedRectangle(cornerRadius: 10))
                        }
                        .buttonStyle(.plain)
                        .accessibilityIdentifier("forYouChip-\(chip.id)")
                    }
                }
            }
            .accessibilityIdentifier("forYouCard")
        }
    }
}

// MARK: - Community

struct BrewCommunityCard: View {
    private var model: BrewModel { BrewModel.shared }
    @State private var confirmStart: Int?

    private var dial: BrewFormat.Dial { model.dial }

    var body: some View {
        BrewCard {
            VStack(alignment: .leading, spacing: 10) {
                HStack(alignment: .firstTextBaseline) {
                    Text("COMMUNITY").font(.subheadline).foregroundStyle(brewPal.dim)
                    Spacer()
                    Button("More") { BrewNavigator.shared.open(.community) }
                        .font(.subheadline)
                        .accessibilityIdentifier("communityMore")
                }
                content
            }
        }
        .accessibilityIdentifier("communityCard")
        .alert("Replace your setting?", isPresented: Binding(get: { confirmStart != nil }, set: { if !$0 { confirmStart = nil } })) {
            Button("Replace") {
                if let um = confirmStart { Task { await model.useStart(um) } }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Replace your setting of \(dial.setting(model.activePreset?.grindUm ?? 0)) with \(dial.setting(confirmStart ?? 0))?")
        }
    }

    @ViewBuilder private var content: some View {
        if let coffee = model.activeCoffee, let recipe = model.activeRecipeDef {
            if let info = model.community {
                if coffee.libraryId.isEmpty {
                    note("\(coffee.name) is not in the shared library yet. Once it is published, where other owners settle shows here.")
                    similarBlock(info)
                } else if info.found != true {
                    note(notFound(coffee, recipe, info))
                    similarBlock(info)
                } else {
                    found(info, recipe)
                    similarBlock(info)
                }
            } else {
                note("What other owners settled on cannot be read from the controller right now.")
            }
        } else {
            note("No coffee is attached to what is being brewed, so there is nothing from other people to show.")
        }
    }

    private func note(_ text: String) -> some View {
        Text(text).font(.subheadline).foregroundStyle(brewPal.dim)
            .accessibilityIdentifier("communityNote")
    }

    private func notFound(_ coffee: CoffeeModel, _ recipe: RecipeDefModel, _ info: BrewCommunityInfo) -> String {
        var text = "Nobody has shared a settled dial-in for \(coffee.name) with a recipe like \(recipe.name) "
            + "(\(BrewFormat.decimal(recipe.doseG, 0)) g in, \(BrewFormat.decimal(recipe.targetWeightG, 0)) g out) yet."
        if info.reachable != true { text += " The shared library cannot be reached from this controller yet." }
        return text
    }

    // MARK: Found

    private func found(_ info: BrewCommunityInfo, _ recipe: RecipeDefModel) -> some View {
        let start = info.start
        let setting = start?.setting ?? 0
        let people = info.contributors ?? 0
        let dialins = info.dialins ?? 0
        return VStack(alignment: .leading, spacing: 10) {
            tile("Start your grinder at",
                 setting > 0 ? "\(dial.setting(setting)) ± \(dial.delta(start?.range ?? 0)) \(dial.deltaUnit)" : "not known",
                 why(info))
                .accessibilityIdentifier("communityStartTile")
            tile("What a good shot looked like", BrewFormat.decimal(info.timeS ?? 0, 1) + " s",
                 "\(BrewFormat.decimal(recipe.doseG, 0)) g in, \(BrewFormat.decimal(recipe.targetWeightG, 0)) g out (\(recipe.name))")
                .accessibilityIdentifier("communityGoodShotTile")
            tile("Based on", "\(people) \(people == 1 ? "person" : "people")",
                 "\(dialins) settled dial-in\(dialins == 1 ? "" : "s"), within ± \(BrewFormat.decimal(info.spreadUm ?? 0, 1)) µm of each other")
            if setting > 0 { useButton(setting, current: model.activePreset?.grindUm ?? 0, similar: false) }
        }
    }

    private func tile(_ title: String, _ value: String, _ detail: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title).font(.footnote).foregroundStyle(brewPal.dim)
            Text(value).font(.title3.weight(.semibold)).foregroundStyle(brewPal.text)
            Text(detail).font(.footnote).foregroundStyle(brewPal.dim)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(brewPal.track, in: RoundedRectangle(cornerRadius: 10))
    }

    private func useButton(_ setting: Int, current: Int, similar: Bool) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Button("Use \(dial.setting(setting)) as my starting grind") {
                if current != 0 && current != setting { confirmStart = setting } else { Task { await model.useStart(setting) } }
            }
            .buttonStyle(BrewKeyStyle(kind: .accent))
            .accessibilityIdentifier(similar ? "communityUseSimilar" : "communityUseStart")
            Text((current != 0 ? "Your setting for it now: \(dial.settingUnit(current))." : "No setting entered for it yet.")
                 + (similar ? "" : " A start only: grind advice takes it from there."))
                .font(.footnote).foregroundStyle(brewPal.dim)
        }
    }

    /// Why the start setting is what it is (the web page's `why`): the basis, then the age and flow shifts.
    private func why(_ info: BrewCommunityInfo) -> String {
        let start = info.start
        var text: String
        let common = info.coffeesInCommon ?? 0
        let same = info.sameModel ?? 0
        switch start?.basis ?? "" {
        case "fitted": text = "translated to your grinder through \(common) coffee\(common == 1 ? "" : "s") you have in common with others"
        case "same_model": text = "what \(same) other\(same == 1 ? "" : "s") with your grinder model set"
        case "model": text = "translated to your grinder through what the library has learned from other owners of your grinder model"
        case "microns": text = "the shared scale read as microns; how your own grinder sits against it is not known yet"
        default: text = "cannot be translated to your grinder yet"
        }
        let mine = info.myAgeDays ?? -1
        let theirs = info.theirAgeDays ?? -1
        if let shift = start?.ageShift, shift != 0 {
            text += "; includes \(dial.delta(shift)) \(dial.deltaUnit) \(shift < 0 ? "finer" : "coarser") because your bag is \(mine) days off roast and theirs were about \(theirs)"
        } else if theirs >= 0, mine >= 0, abs(mine - theirs) >= 7, info.ageRateKnown != true {
            text += "; your bag is \(mine) days off roast, theirs were about \(theirs), and the library has not yet measured what that difference is worth"
        }
        if let flow = start?.flowShift, flow != 0 {
            text += "; includes \(dial.delta(flow)) \(dial.deltaUnit) \(flow < 0 ? "finer" : "coarser") because your recipe aims for a \(flow < 0 ? "slower" : "faster") flow than theirs"
        }
        if info.recipeMatch == "near" {
            text += "; nobody has settled this exact recipe, so this comes from \(BrewFormat.decimal(info.theirDoseG ?? 0, 1)) g in, \(BrewFormat.decimal(info.theirYieldG ?? 0, 1)) g out, with a wider range"
        }
        return text
    }

    // MARK: A guess from the owner's own coffees

    @ViewBuilder private func similarBlock(_ info: BrewCommunityInfo) -> some View {
        if let similar = info.similar, similar.found == true, let setting = similar.setting, setting > 0 {
            VStack(alignment: .leading, spacing: 10) {
                tile("A guess from your own coffees",
                     "\(dial.setting(setting)) ± \(dial.delta(similar.range ?? 0)) \(dial.deltaUnit)",
                     "From \(similar.fromCoffee ?? "") (\(dial.settingUnit(similar.fromSetting ?? 0))), your nearest coffee"
                     + (similar.nearRecipe == true ? " on a close recipe" : "")
                     + ". Grind advice takes over from your first shot.")
                    .accessibilityIdentifier("communitySimilarTile")
                DisclosureGroup("How this guess is made") {
                    VStack(alignment: .leading, spacing: 6) {
                        ForEach(Array(howLines(similar).enumerated()), id: \.offset) { _, line in
                            Text("• " + line).font(.footnote).foregroundStyle(brewPal.dim)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .font(.subheadline)
                .tint(brewPal.accent)
                useButton(setting, current: model.activePreset?.grindUm ?? 0, similar: true)
            }
        }
    }

    private func howLines(_ s: BrewCommunityInfo.Similar) -> [String] {
        var lines: [String] = []
        let samples = s.samples ?? 0
        let applied = s.applied ?? 0
        var factors: [String] = []
        if applied & 1 != 0 { factors.append("roast level") }
        if applied & 2 != 0 { factors.append("altitude") }
        if applied & 4 != 0 { factors.append("process") }
        if s.basis == "fitted" {
            let shift = s.shift ?? 0
            lines.append("Moved \(dial.delta(shift)) \(dial.deltaUnit) \(shift < 0 ? "finer" : "coarser") from it because your own \(samples) coffees show that \(factors.joined(separator: ", ")) changes your setting.")
        } else {
            lines.append("Not moved: no published figure says how far to move for roast, altitude or process, and your own coffees do not show it yet.")
            var hints: [String] = []
            if let h = s.hintRoast, h != 0 {
                hints.append("roasted \(h > 0 ? "darker" : "lighter") than it (\(h > 0 ? "usually ground coarser" : "usually ground finer"))")
            }
            if let h = s.hintAltitude, h != 0 {
                hints.append("grown \(h < 0 ? "higher" : "lower") (\(h < 0 ? "denser, usually ground finer" : "usually ground coarser"))")
            }
            if let h = s.hintProcess, h != 0 {
                hints.append("processed \(h > 0 ? "more like a natural" : "more like a washed") (advice differs and nothing measured supports it)")
            }
            if !hints.isEmpty { lines.append("This coffee is " + hints.joined(separator: " and ") + ".") }
        }
        lines.append(samples >= 2
                     ? "The range comes from how far your own settings are spread (\(samples) coffees)."
                     : "With one coffee to go on the range is wide on purpose.")
        return lines
    }
}
