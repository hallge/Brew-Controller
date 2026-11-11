// The Brew tab: shows the panel the controller says the board would show (/api/screen "panel"):
// home, brew, result, fault or a grind question. The recipe list, the edit panel and the coffee
// details open as sheets. A haptic on stop and on done.
import SwiftUI
import UIKit

/// Where the edit sheet opens: which field, and a grind setting to start from (the no-grind question's SET).
struct BrewEditRoute: Identifiable {
    let id = UUID()
    var field: Int
    var grindUm: Int?
}

struct BrewTab: View {
    @State private var showRecipes = false
    @State private var showCoffee = false
    @State private var edit: BrewEditRoute?

    private var live: LiveModel { LiveModel.shared }
    private var model: BrewModel { BrewModel.shared }

    /// Reload what the tab shows besides the live status when one of these changes.
    private var refreshKey: String {
        "\(live.screen?.panel ?? "")|\(live.live.lastShotID)|\(live.live.activeRecipe)"
    }

    private var anySheet: Bool { showRecipes || showCoffee || edit != nil }

    var body: some View {
        ZStack {
            brewPal.bg.ignoresSafeArea()
            if let screen = live.screen {
                panel(screen)
            } else {
                VStack(spacing: 12) {
                    ProgressView().tint(brewPal.accent)
                    Text(live.screenError?.message ?? "Starting the controller…")
                        .foregroundStyle(brewPal.dim)
                        .accessibilityIdentifier("brewStarting")
                }
            }
        }
        .task(id: refreshKey) { await model.refreshAll() }
        .sheet(isPresented: $showRecipes) { BrewRecipePickerView() }
        .sheet(isPresented: $showCoffee) { BrewCoffeeDetailsView() }
        .sheet(item: $edit) { route in editSheet(route) }
        .onChange(of: live.live.stateNumber) { _, new in
            // The haptics of the phone: a firm tap when the shot is stopped, a success buzz when it is done.
            switch BrewState(rawValue: new) {
            case .stoppingShot: UIImpactFeedbackGenerator(style: .heavy).impactOccurred()
            case .shotComplete: UINotificationFeedbackGenerator().notificationOccurred(.success)
            default: break
            }
        }
        .alert("Not done", isPresented: Binding(get: { model.errorMessage != nil && !anySheet },
                                                set: { if !$0 { model.errorMessage = nil } })) {
            Button("OK") { model.errorMessage = nil }
        } message: {
            Text(model.errorMessage ?? "")
        }
        .accessibilityIdentifier("brewTab")
    }

    @ViewBuilder private func panel(_ screen: ScreenSnapshot) -> some View {
        switch screen.panelKind {
        case .home:
            BrewHomePanel(screen: screen, onRecipes: { showRecipes = true }, onEdit: { openEdit(field: BrewRecipeDraft.grindField) },
                      onCoffee: { showCoffee = true })
        case .brew:
            BrewLiveShotPanel(screen: screen)
        case .result:
            BrewResultPanel(screen: screen, onGrinder: { openEdit(field: BrewRecipeDraft.grindField) })
        case .fault:
            BrewFaultPanel(screen: screen)
        case .grindPrompt:
            BrewGrindPromptPanel(screen: screen, onEditGrind: { um in openEdit(field: BrewRecipeDraft.grindField, grindUm: um) })
        }
    }

    private func openEdit(field: Int, grindUm: Int? = nil) {
        edit = BrewEditRoute(field: field, grindUm: grindUm)
    }

    @ViewBuilder private func editSheet(_ route: BrewEditRoute) -> some View {
        if let library = model.library, let preset = library.activePresetEntry, let recipe = library.recipe(id: preset.recipeId) {
            BrewEditRecipeView(index: library.activePreset, draft: draft(route, preset, recipe, library.grinder), field: route.field)
        } else {
            VStack(spacing: 12) {
                ProgressView()
                Text("Loading the recipe…")
            }
            .padding()
        }
    }

    private func draft(_ route: BrewEditRoute, _ preset: PresetModel, _ recipe: RecipeDefModel, _ grinder: GrinderModel) -> BrewRecipeDraft {
        var draft = BrewRecipeDraft(preset: preset, recipe: recipe, grinder: grinder)
        if let um = route.grindUm, um > 0 { draft.grindUm = um }
        return draft
    }
}
