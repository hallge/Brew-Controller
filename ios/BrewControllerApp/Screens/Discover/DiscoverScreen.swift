// Discover (web page: #discover): who it is for, coffees you might like, subscriptions you might like,
// the coffee library and the recipe book. Pushed by the More tab inside its NavigationStack.
import SwiftUI

struct DiscoverScreen: View {
    @State private var store = DiscoverStore()
    @State private var recipeDraft: DiscoverRecipeDraft?

    var body: some View {
        let palette = BrewTheme.shared.palette
        ScrollView {
            VStack(spacing: 14) {
                if let notice = store.notice {
                    Text(notice.text)
                        .font(.subheadline)
                        .foregroundStyle(palette.text)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(12)
                        .background((notice.good ? palette.ok : palette.bad).opacity(0.25),
                                    in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                        .accessibilityIdentifier("discoverNotice")
                }
                if store.phase == .loading {
                    ProgressView("Loading").frame(maxWidth: .infinity, minHeight: 140)
                        .accessibilityIdentifier("discoverLoading")
                } else {
                    who
                    DiscoverCoffeeRecommendationsCard(store: store)
                    DiscoverSubscriptionRecommendationsCard(store: store)
                    DiscoverCoffeeLibraryCard(store: store)
                    DiscoverRecipeBookCard(store: store) { draft in recipeDraft = draft }
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
        }
        .background(palette.bg)
        .navigationTitle("Discover")
        .refreshable { await store.load() }
        .task { await store.load() }
        .task(id: store.notice?.id) {
            guard store.notice != nil else { return }
            try? await Task.sleep(for: .seconds(5))
            if !Task.isCancelled { store.notice = nil }
        }
        .sheet(item: $recipeDraft) { draft in
            DiscoverRecipeSheet(draft: draft) { checked in await store.makeRecipe(checked) }
        }
        .accessibilityIdentifier("discoverScreen")
    }

    /// Household or a named drinker.
    @ViewBuilder
    private var who: some View {
        let people = store.ratings.people
        if !people.isEmpty {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    chip("Household", id: 0)
                    ForEach(people, id: \.id) { person in chip(person.name, id: person.id) }
                }
            }
            .accessibilityIdentifier("discoverWho")
        }
    }

    private func chip(_ title: String, id: Int) -> some View {
        let palette = BrewTheme.shared.palette
        return Button(title) { Task { await store.chooseDrinker(id) } }
            .buttonStyle(.bordered)
            .tint(id == store.drinker ? palette.accent : palette.dim)
            .accessibilityAddTraits(id == store.drinker ? .isSelected : [])
            .accessibilityIdentifier("discoverWho\(id)")
    }
}

/// A titled card.
struct DiscoverCard<Content: View>: View {
    let title: String
    var subtitle = ""
    let identifier: String
    @ViewBuilder let content: () -> Content

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text(title).font(.headline).foregroundStyle(palette.text).accessibilityAddTraits(.isHeader)
                Spacer(minLength: 0)
                if !subtitle.isEmpty { Text(subtitle).font(.footnote).foregroundStyle(palette.dim) }
            }
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(palette.panel, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(identifier)
    }
}

/// What a list says when there is nothing to show.
struct DiscoverEmptyNote: View {
    let text: String
    let identifier: String

    var body: some View {
        Text(text)
            .font(.subheadline)
            .foregroundStyle(BrewTheme.shared.palette.dim)
            .frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityIdentifier(identifier)
    }
}

/// "2 more coffees": what is still needed before recommendations appear (3 rated coffees).
func discoverMoreCoffees(_ rated: Int) -> String {
    let count = max(1, 3 - rated)
    return "\(count) more coffee" + (count == 1 ? "" : "s")
}
