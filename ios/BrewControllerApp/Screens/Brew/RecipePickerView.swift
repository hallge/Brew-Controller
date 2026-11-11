// The board's RECIPES list (tap the home card) and its COFFEE panel with BAG FINISHED.
// The recipe line is ui/ScreenText.h recipeListLine; the coffee rows are Ui.cpp refreshCoffee.
import SwiftUI

// MARK: - Recipes

struct RecipePickerView: View {
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

    var body: some View {
        NavigationStack {
            ZStack {
                pal.bg.ignoresSafeArea()
                ScrollView {
                    LazyVStack(spacing: 8) {
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
                                .foregroundStyle(active ? pal.accent : pal.text)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(12)
                                .background(active ? pal.select : pal.panel, in: RoundedRectangle(cornerRadius: 10))
                            }
                            .buttonStyle(.plain)
                            .accessibilityIdentifier("recipeRow-\(entry.index)")
                        }
                        if entries.isEmpty {
                            Text("No recipes yet.").foregroundStyle(pal.dim).padding()
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

struct CoffeeDetailsView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var asking = false
    private var model: BrewModel { BrewModel.shared }

    private static let roastNames = ["", "light", "medium-light", "medium", "medium-dark", "dark"]

    private func date(_ ymd: Int) -> String {
        String(format: "%04d-%02d-%02d", ymd / 10_000, (ymd / 100) % 100, ymd % 100)
    }

    /// Rows with a name and a value; what was never entered is left out.
    private var rows: [(String, String)] {
        guard let coffee = model.activeCoffee else { return [] }
        var list: [(String, String)] = [("Roaster", coffee.roaster)]
        if !coffee.name.isEmpty { list.append(("Coffee", coffee.name)) }
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
                pal.bg.ignoresSafeArea()
                ScrollView {
                    VStack(alignment: .leading, spacing: 14) {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(title).font(.title2.weight(.semibold)).foregroundStyle(pal.accent)
                                .accessibilityIdentifier("coffeeTitle")
                            if !subtitle.isEmpty {
                                Text(subtitle).foregroundStyle(pal.text)
                            }
                        }
                        BrewCard {
                            VStack(alignment: .leading, spacing: 8) {
                                if rows.isEmpty {
                                    Text(model.activeCoffee == nil
                                         ? "No coffee is attached to this recipe.\nAdd one on the Coffees tab."
                                         : "Nothing more is known about this coffee.\nAdd details on the Coffees tab.")
                                        .foregroundStyle(pal.dim)
                                }
                                ForEach(Array(rows.enumerated()), id: \.offset) { _, row in
                                    HStack(alignment: .top) {
                                        Text(row.0).foregroundStyle(pal.dim).frame(width: 100, alignment: .leading)
                                        Text(row.1).foregroundStyle(pal.text)
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
