// The shared library on Discover: the coffee library and the recipe book, each with the published ones
// (add, make it a recipe) and the ones waiting for review (approve, or decline with a note).
import SwiftUI

private enum DiscoverLibraryTab: String, CaseIterable, Identifiable {
    case published, waiting
    var id: String { rawValue }
}

struct DiscoverCoffeeLibraryCard: View {
    let store: DiscoverStore
    @State private var tab: DiscoverLibraryTab = .published
    @State private var query = ""

    var body: some View {
        let palette = BrewTheme.shared.palette
        let have = store.haveLibraryIds
        let needle = query.lowercased().trimmingCharacters(in: .whitespaces)
        let hits = store.coffees.filter { coffee in
            needle.isEmpty || [coffee.name, coffee.roaster, coffee.origin, coffee.region, coffee.process, coffee.varietal]
                .joined(separator: " ").lowercased().contains(needle)
        }
        let waiting = store.pendingCoffees
        DiscoverCard(title: "Coffee library", identifier: "discoverCoffeeLibrary") {
            Picker("Which coffees", selection: $tab) {
                Text("Published" + (store.coffees.isEmpty ? "" : " (\(store.coffees.count))")).tag(DiscoverLibraryTab.published)
                Text("Waiting for review" + (waiting.isEmpty ? "" : " (\(waiting.count))")).tag(DiscoverLibraryTab.waiting)
            }
            .pickerStyle(.segmented)
            .accessibilityIdentifier("discoverCoffeeLibraryTab")
            if tab == .published {
                TextField("Search: name, roaster, origin, process", text: $query)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("discoverCoffeeSearch")
                if store.coffees.isEmpty {
                    DiscoverEmptyNote(text: "No coffees published yet. Published coffees appear here, ready to add to your shelf.",
                                      identifier: "discoverCoffeeLibraryEmpty")
                } else if hits.isEmpty {
                    DiscoverEmptyNote(text: "No shared coffee matches that.", identifier: "discoverCoffeeLibraryNoMatch")
                } else {
                    ForEach(hits.prefix(40)) { coffee in
                        HStack(alignment: .top, spacing: 8) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(coffee.name).font(.subheadline.weight(.semibold)).foregroundStyle(palette.text)
                                Text(coffee.detail).font(.footnote).foregroundStyle(palette.dim)
                            }
                            Spacer(minLength: 0)
                            if have.contains(coffee.id) {
                                Text("on your shelf").font(.footnote).foregroundStyle(palette.dim)
                            } else {
                                Button("Add to my coffees") { Task { await store.adopt(coffee) } }
                                    .buttonStyle(.bordered)
                                    .font(.footnote)
                                    .accessibilityIdentifier("discoverCoffeeAdd.\(coffee.id)")
                            }
                        }
                        .accessibilityElement(children: .contain)
                    }
                    if hits.count > 40 {
                        Text("and \(hits.count - 40) more: search to narrow it down.").font(.footnote).foregroundStyle(palette.dim)
                    }
                }
            } else {
                Text(store.pendingWhy(recipes: false)).font(.footnote).foregroundStyle(palette.dim)
                    .accessibilityIdentifier("discoverCoffeeWaitingWhy")
                if waiting.isEmpty {
                    DiscoverEmptyNote(text: "Nothing waiting for review.", identifier: "discoverCoffeeWaitingEmpty")
                } else {
                    ForEach(waiting) { item in DiscoverPendingRow(store: store, item: item) }
                }
            }
        }
    }
}

struct DiscoverRecipeBookCard: View {
    let store: DiscoverStore
    let onMake: (DiscoverRecipeDraft) -> Void
    @State private var tab: DiscoverLibraryTab = .published
    @State private var query = ""

    var body: some View {
        let palette = BrewTheme.shared.palette
        let needle = query.lowercased().trimmingCharacters(in: .whitespaces)
        let hits = store.recipes.filter { entry in
            needle.isEmpty || (entry.recipe.name + " " + entry.recipe.line).lowercased().contains(needle)
        }
        let waiting = store.pendingRecipes
        DiscoverCard(title: "Recipe book", identifier: "discoverRecipeBook") {
            Picker("Which recipes", selection: $tab) {
                Text("Published" + (store.recipes.isEmpty ? "" : " (\(store.recipes.count))")).tag(DiscoverLibraryTab.published)
                Text("Waiting for review" + (waiting.isEmpty ? "" : " (\(waiting.count))")).tag(DiscoverLibraryTab.waiting)
            }
            .pickerStyle(.segmented)
            .accessibilityIdentifier("discoverRecipeBookTab")
            if tab == .published {
                TextField("Search: name, dose, yield", text: $query)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("discoverRecipeSearch")
                if store.recipes.isEmpty {
                    DiscoverEmptyNote(text: "No recipes published yet. Share one of yours from the Recipes page.",
                                      identifier: "discoverRecipeBookEmpty")
                } else if hits.isEmpty {
                    DiscoverEmptyNote(text: "No recipe matches that.", identifier: "discoverRecipeBookNoMatch")
                } else {
                    ForEach(hits) { entry in
                        HStack(alignment: .top, spacing: 8) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(entry.recipe.name.isEmpty ? "Recipe" : entry.recipe.name)
                                    .font(.subheadline.weight(.semibold)).foregroundStyle(palette.text)
                                Text(entry.recipe.line + (entry.contributors > 1 ? " · \(entry.contributors) owners" : ""))
                                    .font(.footnote).foregroundStyle(palette.dim)
                            }
                            Spacer(minLength: 0)
                            Button("Make it a recipe") {
                                onMake(DiscoverRecipeDraft(name: entry.recipe.name.isEmpty ? "Recipe" : entry.recipe.name,
                                                           doseG: entry.recipe.doseG, yieldG: entry.recipe.yieldG,
                                                           timeS: entry.recipe.targetTimeS,
                                                           holdS: entry.recipe.preinfusionHoldS,
                                                           bloomS: entry.recipe.bloomPauseS))
                            }
                            .buttonStyle(.bordered)
                            .font(.footnote)
                        }
                    }
                }
            } else {
                Text(store.pendingWhy(recipes: true)).font(.footnote).foregroundStyle(palette.dim)
                    .accessibilityIdentifier("discoverRecipeWaitingWhy")
                if waiting.isEmpty {
                    DiscoverEmptyNote(text: "Nothing waiting for review.", identifier: "discoverRecipeWaitingEmpty")
                } else {
                    ForEach(waiting) { item in DiscoverPendingRow(store: store, item: item) }
                }
            }
            DisclosureGroup {
                Text("• An item is published once two owners approve it; one decline holds it for the library's maintainer.\n• Your reviews count once something you shared has been published. You never review your own.\n• Share one of your recipes from the Recipes page.")
                    .font(.footnote).foregroundStyle(palette.dim)
            } label: {
                Text("Owners review what other owners send.").font(.footnote).foregroundStyle(palette.dim)
            }
        }
    }
}

/// One item waiting for review.
struct DiscoverPendingRow: View {
    let store: DiscoverStore
    let item: DiscoverPendingItem
    @State private var declining = false
    @State private var note = ""

    var body: some View {
        let palette = BrewTheme.shared.palette
        let isCoffee = item.type == "coffee"
        let own = !(store.pending?.kid ?? "").isEmpty && store.pending?.kid == item.from
        let verdict = store.pending?.myVerdict(for: item.id)
        let canReview = store.pending?.canReview ?? false
        VStack(alignment: .leading, spacing: 4) {
            Text(isCoffee ? (item.coffee.name.isEmpty ? "Coffee" : item.coffee.name)
                 : (item.recipe.name.isEmpty ? "Recipe" : item.recipe.name))
                .font(.subheadline.weight(.semibold)).foregroundStyle(palette.text)
            Text(isCoffee
                 ? [item.coffee.roaster, item.coffee.origin, item.coffee.region, item.coffee.process, item.coffee.roastLevel]
                    .filter { !$0.isEmpty }.joined(separator: " · ")
                 : item.recipe.line)
                .font(.footnote).foregroundStyle(palette.dim)
            if isCoffee && !item.coffee.notes.isEmpty {
                Text(item.coffee.notes).font(.footnote).foregroundStyle(palette.dim)
            }
            Text("\(item.approvals) of 2 approvals" + (item.declines > 0 ? ", \(item.declines) declined" : ""))
                .font(.footnote).foregroundStyle(palette.dim)
            if own {
                Text("yours, waiting").font(.caption.weight(.semibold)).foregroundStyle(palette.accent)
            } else if let verdict {
                Text("you " + (verdict ? "approved" : "declined")).font(.caption.weight(.semibold))
                    .foregroundStyle(verdict ? palette.ok : palette.accent)
            } else if canReview {
                HStack {
                    Button("Approve") { Task { _ = await store.review(item, approve: true, note: "") } }
                        .buttonStyle(.borderedProminent)
                        .accessibilityIdentifier("discoverApprove.\(item.id)")
                    Button("Decline…") { declining.toggle() }
                        .buttonStyle(.bordered)
                        .accessibilityIdentifier("discoverDecline.\(item.id)")
                }
                .font(.subheadline)
                if declining {
                    HStack {
                        TextField("Why? The sender sees this.", text: $note)
                            .textFieldStyle(.roundedBorder)
                            .onChange(of: note) { _, text in if text.count > 119 { note = String(text.prefix(119)) } }
                            .accessibilityIdentifier("discoverDeclineNote.\(item.id)")
                        Button("Send") {
                            Task {
                                if await store.review(item, approve: false, note: note) {
                                    declining = false
                                    note = ""
                                }
                            }
                        }
                        .buttonStyle(.bordered)
                        .accessibilityIdentifier("discoverDeclineSend.\(item.id)")
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(palette.track.opacity(0.4), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("discoverPending.\(item.id)")
    }
}
