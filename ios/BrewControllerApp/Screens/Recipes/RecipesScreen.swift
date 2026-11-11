// Recipes (W: #recipes): the list, and a form for each recipe. The shell puts this in a NavigationStack
// (the More tab pushes it), so there is none here.
import SwiftUI

struct RecipesScreen: View {
    @State private var store = RecipeStore()

    var body: some View {
        List {
            Section {
                if store.recipes.isEmpty {
                    Text(store.isLoading ? "Loading..." : "No recipes yet.")
                        .foregroundStyle(.secondary)
                        .accessibilityIdentifier("recipesEmpty")
                }
                ForEach(store.recipes) { recipe in
                    NavigationLink {
                        RecipeFormView(store: store, recipeId: recipe.id)
                    } label: {
                        RecipeRow(recipe: recipe, entries: store.entryCount(recipe.id))
                    }
                    .accessibilityIdentifier("recipeRow-\(recipe.id)")
                }
            } footer: {
                Text("A recipe says how to pull the shot. It holds nothing about the coffee or the grind, so one recipe serves every coffee.")
            }
        }
        .accessibilityIdentifier("recipesList")
        .recipeThemed()
        .navigationTitle("Recipes")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                NavigationLink {
                    RecipeFormView(store: store, recipeId: 0)
                } label: {
                    Label("New recipe", systemImage: "plus")
                }
                .accessibilityIdentifier("recipeNew")
            }
        }
        .task { await store.load() }
        .refreshable { await store.load() }
        .recipeNotices(store)
    }
}

/// "Double", "18.0 g in, 36.0 g out · 1 entry on the controller".
private struct RecipeRow: View {
    let recipe: RecipeDefModel
    let entries: Int

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(recipe.name).font(.headline)
            Text("\(BrewFormat.grams(recipe.doseG)) g in, \(BrewFormat.grams(recipe.targetWeightG)) g out · \(usage)")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
    }

    private var usage: String {
        entries == 0 ? "not on the controller" : "\(entries) \(entries == 1 ? "entry" : "entries") on the controller"
    }
}

// MARK: - Look and notices

extension View {
    /// The board's colours behind a list or form.
    @MainActor
    func recipeThemed() -> some View {
        let palette = BrewTheme.shared.palette
        return self
            .scrollContentBackground(.hidden)
            .background(palette.bg.ignoresSafeArea())
            .tint(palette.accent)
    }

    /// The store's error as an alert and its notice as a short banner at the bottom.
    @MainActor
    func recipeNotices(_ store: RecipeStore) -> some View {
        modifier(RecipeNoticeModifier(store: store))
    }
}

private struct RecipeNoticeModifier: ViewModifier {
    let store: RecipeStore

    func body(content: Content) -> some View {
        content
            .overlay(alignment: .bottom) {
                if let notice = store.notice {
                    Text(notice)
                        .font(.subheadline)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(.thinMaterial, in: Capsule())
                        .padding(.bottom, 12)
                        .transition(.opacity)
                        .accessibilityIdentifier("recipeNotice")
                }
            }
            .animation(.default, value: store.notice)
            .alert("Brew Controller", isPresented: Binding(
                get: { store.errorText != nil },
                set: { if !$0 { store.errorText = nil } }
            )) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(store.errorText ?? "")
            }
    }
}
