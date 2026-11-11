// The Brew tab: shows the panel the controller says the board would show (/api/screen "panel"):
// home, brew, result, fault or a grind question. The recipe list, the edit panel, the coffee details and
// the board's Settings open as sheets; an open shot, Getting started, Share to Reddit, Discover and
// Community are pushed (BrewNavigator.route). A haptic on stop and on done. What the tab shows is
// reloaded whenever it appears, so a change made on another tab is never shown (or saved back) old.
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
    @State private var showSettings = false
    @State private var edit: BrewEditRoute?
    @State private var appearances = 0
    @State private var nav = BrewNavigator.shared

    private var live: LiveModel { LiveModel.shared }
    private var model: BrewModel { BrewModel.shared }

    /// Reload what the tab shows besides the live status when one of these changes (or the tab appears).
    private var refreshKey: String {
        "\(live.screen?.panel ?? "")|\(live.live.lastShotID)|\(live.live.activeRecipe)|\(appearances)"
    }

    private var anySheet: Bool { showRecipes || showCoffee || showSettings || edit != nil }

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
        .onAppear { appearances += 1 }
        .task(id: refreshKey) { await model.refreshAll() }
        .task {
            // The machine's state is polled every 5 s while Brew is on show (the web page does the same).
            while !Task.isCancelled {
                if !live.live.isShotInHand { await model.refreshPower() }
                try? await Task.sleep(for: .seconds(5))
            }
        }
        .sheet(isPresented: $showRecipes) { BrewRecipePickerView() }
        .sheet(isPresented: $showCoffee) { BrewCoffeeDetailsView() }
        .sheet(isPresented: $showSettings) { BrewSettingsView() }
        .sheet(item: $edit) { route in editSheet(route) }
        .navigationDestination(item: $nav.route) { route in destination(route) }
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
        .alert("Saved", isPresented: Binding(get: { model.noticeMessage != nil },
                                             set: { if !$0 { model.noticeMessage = nil } })) {
            Button("OK") { model.noticeMessage = nil }
        } message: {
            Text(model.noticeMessage ?? "")
        }
        .accessibilityIdentifier("brewTab")
    }

    @ViewBuilder private func panel(_ screen: ScreenSnapshot) -> some View {
        switch screen.panelKind {
        case .home:
            BrewHomePanel(screen: screen, onRecipes: { showRecipes = true },
                          onEdit: { openEdit(field: BrewRecipeDraft.grindField) },
                          onCoffee: { showCoffee = true }, onSettings: { showSettings = true })
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

    // MARK: Pushed screens

    @ViewBuilder private func destination(_ route: BrewRoute) -> some View {
        switch route {
        case .shot(let id): ShotsDetailScreen(shotID: id)
        case .started: SetupStartedScreen()
        case .reddit(let id): CommunityScreen(redditShotID: id)
        case .community: CommunityScreen()
        case .discover: DiscoverScreen()
        }
    }

    // MARK: Edit

    /// The library is read again first: the edit sheet must not open on old values and save them back.
    /// While a shot is in hand the controller does not answer for the library, so the last one is used then.
    private func openEdit(field: Int, grindUm: Int? = nil) {
        Task {
            let fresh = await model.refreshLibrary()
            if fresh || (live.live.isShotInHand && model.library != nil) {
                edit = BrewEditRoute(field: field, grindUm: grindUm)
            } else {
                model.errorMessage = "The recipe could not be read from the controller. Try again."
            }
        }
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
