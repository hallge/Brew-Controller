// "Community dial-in" (W: #ccom, `drawCommunity`): where other owners settled on this coffee with a recipe
// like the one being brewed, translated to this owner's grinder, and a guess from the owner's own nearest
// coffee. "Use X as my starting grind" sets the grind of the coffee-and-recipe entry (`preset_grind`).
import SwiftUI

struct CoffeeCommunitySection: View {
    let store: CoffeeStore
    let coffee: CoffeeModel

    @State private var model: CoffeeCommunityModel?
    @State private var loaded = false
    @State private var pendingUse: CoffeeStartChoice?

    /// A setting to put on an entry, waiting for the owner to agree to replace theirs.
    struct CoffeeStartChoice: Identifiable {
        let id = UUID()
        let entry: Int
        let setting: Int
        let current: Int
    }

    /// The controller entries (coffee and recipe pairs) of this coffee.
    private var entries: [(index: Int, preset: PresetModel)] {
        (store.library?.presets ?? []).enumerated().compactMap { index, preset in
            preset.coffeeId == coffee.id ? (index: index, preset: preset) : nil
        }
    }

    /// The entry in use, else the first.
    private var chosen: (index: Int, preset: PresetModel)? {
        entries.first { $0.index == store.library?.activePreset } ?? entries.first
    }

    private var chosenRecipe: RecipeDefModel? {
        chosen.flatMap { store.library?.recipe(id: $0.preset.recipeId) }
    }

    var body: some View {
        Section {
            content
        } header: {
            Text("Community dial-in")
        }
        .task(id: loadKey) { await load() }
        .confirmationDialog(replaceTitle, isPresented: Binding(
            get: { pendingUse != nil }, set: { if !$0 { pendingUse = nil } }
        ), titleVisibility: .visible) {
            Button("Replace") {
                if let choice = pendingUse { Task { await store.useStart(entry: choice.entry, setting: choice.setting) } }
                pendingUse = nil
            }
        }
    }

    private var replaceTitle: String {
        guard let pendingUse else { return "Replace your setting?" }
        return "Replace your setting of " + store.dial.settingUnit(pendingUse.current) + " with "
            + store.dial.settingUnit(pendingUse.setting) + "?"
    }

    /// What the answer depends on: the coffee's library id, the entry and its recipe.
    private var loadKey: String {
        "\(coffee.id)-\(coffee.libraryId)-\(chosen?.index ?? -1)-\(chosen?.preset.recipeId ?? 0)"
    }

    private func load() async {
        model = nil
        loaded = false
        guard let chosen else {
            loaded = true
            return
        }
        model = await store.community(coffee: coffee.id, recipe: chosen.preset.recipeId)
        loaded = true
    }

    // MARK: Content

    @ViewBuilder
    private var content: some View {
        if chosen == nil {
            Text(coffee.libraryId.isEmpty
                 ? coffee.name + " is not in the shared library yet. Once it is published, where other owners settle shows here."
                 : "Tick a recipe for this coffee to see what others settled on for it.")
                .font(.footnote).foregroundStyle(.secondary)
                .accessibilityIdentifier("coffeeCommunityNote")
        } else if let model, let chosen, let recipe = chosenRecipe {
            if coffee.libraryId.isEmpty {
                Text(coffee.name + " is not in the shared library yet. Once it is published, where other owners settle shows here.")
                    .font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("coffeeCommunityNote")
            } else if !model.found {
                Text(notFoundText(model, recipe))
                    .font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("coffeeCommunityNote")
            } else {
                found(model, chosen, recipe)
            }
            similar(model, chosen)
        } else if !loaded {
            HStack { ProgressView(); Text("Asking the shared library...").foregroundStyle(.secondary) }
        } else {
            Text("The controller could not say.").font(.footnote).foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private func found(_ model: CoffeeCommunityModel, _ chosen: (index: Int, preset: PresetModel),
                       _ recipe: RecipeDefModel) -> some View {
        let start = model.start
        let dial = store.dial
        let startValue = start.setting > 0
            ? dial.setting(start.setting) + " ± " + dial.delta(start.range) + " " + dial.deltaUnit : "not known"
        let goodShot = BrewFormat.grams(recipe.doseG) + " g in, " + BrewFormat.grams(recipe.targetWeightG)
            + " g out (" + recipe.name + ")"
        let people = "\(model.contributors) " + (model.contributors == 1 ? "person" : "people")
        CoffeeInfoTile(title: "Start your grinder at", value: startValue, detail: why(model))
        CoffeeInfoTile(title: "What a good shot looked like", value: BrewFormat.seconds(model.timeS) + " s",
                       detail: goodShot)
        CoffeeInfoTile(title: "Based on", value: people, detail: basedOnText(model))
        if start.setting > 0 {
            useButton(start.setting, chosen)
        }
    }

    @ViewBuilder
    private func similar(_ model: CoffeeCommunityModel, _ chosen: (index: Int, preset: PresetModel)) -> some View {
        let guess = model.similar
        let dial = store.dial
        if guess.found {
            CoffeeInfoTile(
                title: "A guess from your own coffees",
                value: dial.setting(guess.setting) + " ± " + dial.delta(guess.range) + " " + dial.deltaUnit,
                detail: "From " + guess.fromCoffee + " (" + dial.settingUnit(guess.fromSetting)
                    + "), your nearest coffee. Grind advice takes over from your first shot.")
            Text(howText(guess))
                .font(.footnote).foregroundStyle(.secondary)
            if guess.setting > 0 { useButton(guess.setting, chosen) }
        }
    }

    private func useButton(_ setting: Int, _ chosen: (index: Int, preset: PresetModel)) -> some View {
        let dial = store.dial
        return VStack(alignment: .leading, spacing: 4) {
            Button("Use " + dial.settingUnit(setting) + " as my starting grind") {
                let current = chosen.preset.grindUm
                if current != 0 && current != setting {
                    pendingUse = CoffeeStartChoice(entry: chosen.index, setting: setting, current: current)
                } else {
                    Task { await store.useStart(entry: chosen.index, setting: setting) }
                }
            }
            .accessibilityIdentifier("coffeeUseStart")
            Text(nowText(chosen.preset.grindUm))
                .font(.footnote).foregroundStyle(.secondary)
        }
    }

    private func notFoundText(_ model: CoffeeCommunityModel, _ recipe: RecipeDefModel) -> String {
        var text = "Nobody has shared a settled dial-in for " + coffee.name + " with a recipe like " + recipe.name
        text += " (" + BrewFormat.grams(recipe.doseG) + " g in, " + BrewFormat.grams(recipe.targetWeightG) + " g out) yet."
        if !model.reachable { text += " The shared library cannot be reached from this controller yet." }
        return text
    }

    private func basedOnText(_ model: CoffeeCommunityModel) -> String {
        var text = "\(model.dialins) settled dial-in" + (model.dialins == 1 ? "" : "s")
        text += ", within ± " + BrewFormat.decimal(model.spreadUm, 1) + " µm of each other"
        return text
    }

    private func howText(_ guess: CoffeeCommunitySimilar) -> String {
        guard guess.basis == "fitted" else {
            return "Not moved: no published figure says how far to move for roast, altitude or process, and your own coffees do not show it yet."
        }
        let dial = store.dial
        var text = "Moved " + dial.delta(guess.shift) + " " + dial.deltaUnit
        text += guess.shift < 0 ? " finer" : " coarser"
        text += " from it because your own coffees show that roast level, altitude or process changes your setting."
        return text
    }

    private func nowText(_ grind: Int) -> String {
        var text = grind > 0 ? "Your setting for it now: " + store.dial.settingUnit(grind) + "." : "No setting entered for it yet."
        text += " A start only: grind advice takes it from there."
        return text
    }

    /// Why the start is what it is (WebPage.h `drawCommunity`).
    private func why(_ model: CoffeeCommunityModel) -> String {
        let dial = store.dial
        let start = model.start
        var text: String
        switch start.basis {
        case "fitted":
            text = "translated to your grinder through \(model.coffeesInCommon) coffee" + (model.coffeesInCommon == 1 ? "" : "s")
                + " you have in common with others"
        case "same_model":
            text = "what \(model.sameModel) other" + (model.sameModel == 1 ? "" : "s") + " with your grinder model set"
        case "model":
            text = "translated to your grinder through what the library has learned from other owners of your grinder model"
        case "microns":
            text = "the shared scale read as microns; how your own grinder sits against it is not known yet"
        default:
            text = "cannot be translated to your grinder yet"
        }
        if start.ageShift != 0 {
            text += "; includes " + dial.delta(start.ageShift) + " " + dial.deltaUnit + (start.ageShift < 0 ? " finer" : " coarser")
                + " because your bag is \(model.myAgeDays) days off roast and theirs were about \(model.theirAgeDays)"
        } else if model.theirAgeDays >= 0, model.myAgeDays >= 0, abs(model.myAgeDays - model.theirAgeDays) >= 7,
                  !model.ageRateKnown {
            text += "; your bag is \(model.myAgeDays) days off roast, theirs were about \(model.theirAgeDays), and the library has not yet measured what that difference is worth"
        }
        if start.flowShift != 0 {
            text += "; includes " + dial.delta(start.flowShift) + " " + dial.deltaUnit + (start.flowShift < 0 ? " finer" : " coarser")
                + " because your recipe aims for a " + (start.flowShift < 0 ? "slower" : "faster") + " flow than theirs"
        }
        if model.recipeMatch == "near" {
            text += "; nobody has settled this exact recipe, so this comes from " + BrewFormat.grams(model.theirDoseG)
                + " g in, " + BrewFormat.grams(model.theirYieldG) + " g out, with a wider range"
        }
        return text
    }
}

/// A title, a big value and a sentence under it.
private struct CoffeeInfoTile: View {
    let title: String
    let value: String
    let detail: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title3.weight(.semibold))
            Text(detail).font(.caption).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .accessibilityElement(children: .combine)
    }
}
