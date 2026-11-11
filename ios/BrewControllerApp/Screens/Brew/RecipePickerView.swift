// The board's RECIPES list (tap the home card) and its COFFEE panel with BAG FINISHED.
// The recipe line is ui/ScreenText.h recipeListLine; the coffee rows are Ui.cpp refreshCoffee.
import SwiftUI

// MARK: - Recipes

struct BrewRecipePickerView: View {
    @Environment(\.dismiss) private var dismiss
    private var model: BrewModel { BrewModel.shared }

    /// One entry of the list: its index in the presets (the controller's own order), and its two lines.
    private struct Entry: Identifiable {
        var index: Int
        var first: String
        var second: String
        var id: Int { index }
    }

    /// Entries of a finished bag stay hidden, except the one in use.
    private var entries: [Entry] {
        guard let library = model.library else { return [] }
        var result: [Entry] = []
        for (index, preset) in library.presets.enumerated() {
            if preset.hidden && index != library.activePreset { continue }
            guard let recipe = library.recipe(id: preset.recipeId) else { continue }
            let coffee = library.coffee(id: preset.coffeeId)
            let out = recipe.useRatio ? recipe.doseG * recipe.ratio : recipe.targetWeightG
            let grind = preset.grindUm > 0 ? "  \(library.grinder.dial.settingUnit(preset.grindUm))" : ""
            let yield = "\(BrewFormat.decimal(recipe.doseG, 1)) -> \(BrewFormat.decimal(out, 1)) g\(grind)"
            if let coffee, !coffee.name.isEmpty {
                result.append(Entry(index: index, first: coffee.name, second: "\(recipe.name)  \(yield)"))
            } else {
                result.append(Entry(index: index, first: recipe.name, second: yield))
            }
        }
        return result
    }

    /// "N entries on the controller (M more wait for a new bag), room for K." as the web page says it.
    private var infoLine: String {
        guard let library = model.library else { return "" }
        let shown = library.presets.filter { !$0.hidden }.count
        let waiting = library.presets.count - shown
        return "\(shown) entries on the controller" + (waiting > 0 ? " (\(waiting) more wait for a new bag)" : "")
            + ", room for \(library.maxPresets)."
    }

    var body: some View {
        NavigationStack {
            ZStack {
                brewPal.bg.ignoresSafeArea()
                ScrollView {
                    LazyVStack(spacing: 8) {
                        Text(infoLine)
                            .font(.footnote).foregroundStyle(brewPal.dim)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .accessibilityIdentifier("recipesInfo")
                        ForEach(entries) { entry in
                            let active = entry.index == model.library?.activePreset
                            Button {
                                Task {
                                    await model.selectRecipe(entry.index)
                                    dismiss()
                                }
                            } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(entry.first).font(.headline)
                                    Text(entry.second).font(.subheadline)
                                }
                                .foregroundStyle(active ? brewPal.accent : brewPal.text)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(12)
                                .background(active ? brewPal.select : brewPal.panel, in: RoundedRectangle(cornerRadius: 10))
                            }
                            .buttonStyle(.plain)
                            .accessibilityIdentifier("recipeRow-\(entry.index)")
                        }
                        if entries.isEmpty {
                            Text("No recipes yet.").foregroundStyle(brewPal.dim).padding()
                        }
                    }
                    .padding(16)
                }
            }
            .navigationTitle("Recipes")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Close") { dismiss() }.accessibilityIdentifier("recipesClose")
                }
            }
        }
        .accessibilityIdentifier("recipePicker")
        .alert("Not changed", isPresented: Binding(get: { model.errorMessage != nil }, set: { if !$0 { model.errorMessage = nil } })) {
            Button("OK") { model.errorMessage = nil }
        } message: {
            Text(model.errorMessage ?? "")
        }
    }
}

// MARK: - Coffee details

struct BrewCoffeeDetailsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var asking = false
    private var model: BrewModel { BrewModel.shared }

    private static let roastNames = ["", "light", "medium-light", "medium", "medium-dark", "dark"]

    private func date(_ ymd: Int) -> String {
        String(format: "%04d-%02d-%02d", ymd / 10_000, (ymd / 100) % 100, ymd % 100)
    }

    /// Rows with a name and a value; what was never entered is left out.
    private var rows: [(String, String)] {
        // No coffee on the shelf: the recipe still remembers a region and a varietal (the board's rows).
        guard let coffee = model.activeCoffee else {
            guard let recipe = recipeEntry else { return [] }
            return [("Region", recipe.region), ("Varietal", recipe.varietal)].filter { !$0.1.isEmpty }
        }
        var list: [(String, String)] = [("Roaster", coffee.roaster)]
        // The coffee's name is the title; it is a row only when the recipe was named for something else.
        if !coffee.name.isEmpty, let recipe = recipeEntry, !recipe.coffee.isEmpty, recipe.coffee != coffee.name {
            list.append(("Coffee", coffee.name))
        }
        list.append(("Country", coffee.origin))
        list.append(("Region", coffee.region))
        list.append(("Producer", coffee.producer))
        list.append(("Varietal", coffee.varietal))
        list.append(("Process", coffee.process))
        if coffee.altitudeM > 0 { list.append(("Altitude", "\(coffee.altitudeM) m")) }
        if coffee.roastDate > 0 { list.append(("Roasted", date(coffee.roastDate))) }
        list.append(("Notes", coffee.notes))
        return list.filter { !$0.1.isEmpty }
    }

    /// The recipe entry in use as the settings list it (it carries the region and varietal noted for it).
    private var recipeEntry: SettingsRecipe? {
        guard let settings = model.settings else { return nil }
        return settings.recipes.first { $0.slot == settings.activeRecipe }
    }

    private var title: String { model.activeCoffee?.name ?? "No coffee" }

    private var subtitle: String {
        guard let coffee = model.activeCoffee else { return "" }
        let level = coffee.roastLevel > 0 && coffee.roastLevel < Self.roastNames.count ? Self.roastNames[coffee.roastLevel] : ""
        let age = LiveModel.shared.screen?.home.card.roastAgeBrief ?? ""
        return [level, age].filter { !$0.isEmpty }.joined(separator: ", ")
    }

    var body: some View {
        NavigationStack {
            ZStack {
                brewPal.bg.ignoresSafeArea()
                ScrollView {
                    VStack(alignment: .leading, spacing: 14) {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(title).font(.title2.weight(.semibold)).foregroundStyle(brewPal.accent)
                                .accessibilityIdentifier("coffeeTitle")
                            if !subtitle.isEmpty {
                                Text(subtitle).foregroundStyle(brewPal.text)
                            }
                        }
                        BrewCard {
                            VStack(alignment: .leading, spacing: 8) {
                                if rows.isEmpty {
                                    Text(model.activeCoffee == nil
                                         ? "No coffee is attached to this recipe.\nAdd one on the Coffees tab."
                                         : "Nothing more is known about this coffee.\nAdd details on the Coffees tab.")
                                        .foregroundStyle(brewPal.dim)
                                }
                                ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                                    HStack(alignment: .top) {
                                        Text(row.0).foregroundStyle(brewPal.dim).frame(width: 100, alignment: .leading)
                                        Text(row.1).foregroundStyle(brewPal.text)
                                        Spacer(minLength: 0)
                                    }
                                }
                            }
                        }
                        if model.activeCoffee != nil {
                            // The first tap asks; a second finishes the bag.
                            Button {
                                if asking {
                                    Task {
                                        let done = await model.finishBag()
                                        asking = false
                                        if done { dismiss() }
                                    }
                                } else {
                                    asking = true
                                    Task {
                                        try? await Task.sleep(for: .seconds(5))
                                        asking = false
                                    }
                                }
                            } label: {
                                Text(asking ? "TAP AGAIN\nTO CONFIRM" : "BAG FINISHED")
                            }
                            .buttonStyle(BrewKeyStyle(kind: asking ? .bad : .track))
                            .accessibilityIdentifier("bagFinishedButton")
                        }
                    }
                    .padding(16)
                }
            }
            .navigationTitle("Coffee")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Back") { dismiss() }.accessibilityIdentifier("coffeeBack")
                }
            }
        }
        .accessibilityIdentifier("coffeeDetails")
    }
}
