// "Coffees you might like" (with Buy again) and "Subscriptions you might like" (country, what matters).
import SwiftUI

struct DiscoverCoffeeRecommendationsCard: View {
    let store: DiscoverStore

    private static let sources = ["shelf": "Your shelf", "library": "Library", "community": "Library ratings"]

    private func stars(_ count: Int) -> String {
        String(repeating: "★", count: max(0, min(5, count))) + String(repeating: "☆", count: max(0, 5 - count))
    }


    private func recoCard(_ item: DiscoverRecoItem) -> some View {
        let palette = BrewTheme.shared.palette
        return VStack(alignment: .leading, spacing: 3) {
            Text(Self.sources[item.from] ?? "").font(.caption).foregroundStyle(palette.dim)
            Text(item.name).font(.headline).foregroundStyle(palette.text)
            let detail = [item.roaster, item.origin, item.process].filter { !$0.isEmpty }.joined(separator: " · ")
            if !detail.isEmpty { Text(detail).font(.footnote).foregroundStyle(palette.dim) }
            if !item.why.isEmpty {
                Text(item.why.joined(separator: ", ")).font(.subheadline.weight(.semibold))
                    .foregroundStyle(palette.text)
            }
            if item.like != 0, let name = store.coffeeName(item.like) {
                Text("Like the \(name) you gave \(stars(item.likeStars))")
                    .font(.footnote).foregroundStyle(palette.dim)
            }
            let extra = [
                item.peopleStars > 0 ? "drinkers like you: ★" + BrewFormat.decimal(item.peopleStars, 1) : "",
                item.communityN > 0 ? "library: ★" + BrewFormat.decimal(item.communityStars, 1) + " from \(item.communityN)" : "",
                item.sure < 0.4 ? "a guess from its description" : "",
            ].filter { !$0.isEmpty }.joined(separator: " · ")
            if !extra.isEmpty { Text(extra).font(.footnote).foregroundStyle(palette.dim) }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(palette.track.opacity(0.4), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
        .accessibilityElement(children: .combine)
    }

    var body: some View {
        let palette = BrewTheme.shared.palette
        let people = store.ratings.people
        DiscoverCard(title: "Coffees you might like",
                     subtitle: store.drinker != 0 ? (people.first { $0.id == store.drinker }?.name ?? "") : "",
                     identifier: "discoverCoffeeRecommendations") {
            if let recommendations = store.recommendations {
                if !recommendations.shown {
                    DiscoverEmptyNote(
                        text: "Rate \(discoverMoreCoffees(recommendations.coffees)) on the Coffees page and recommendations appear here.",
                        identifier: "discoverCoffeeRecommendationsEmpty")
                } else if recommendations.items.isEmpty {
                    DiscoverEmptyNote(
                        text: "Nothing new to recommend yet. Coffees on your shelf that you have not rated, and coffees in the shared library that match your taste, appear here as they come.",
                        identifier: "discoverCoffeeRecommendationsEmpty")
                } else {
                    ForEach(recommendations.items) { item in
                        if item.coffee != 0 {
                            NavigationLink {
                                DiscoverCoffeeDestination(coffeeId: item.coffee)
                            } label: {
                                recoCard(item)
                            }
                            .buttonStyle(.plain)
                            .accessibilityIdentifier("discoverRecoLink.\(item.coffee)")
                        } else {
                            recoCard(item)
                        }
                    }
                }
                if !recommendations.buyAgain.isEmpty {
                    Text("Buy again: " + recommendations.buyAgain.map { "\($0.name) \(stars($0.stars))" }.joined(separator: ", "))
                        .font(.subheadline).foregroundStyle(palette.text)
                        .accessibilityIdentifier("discoverBuyAgain")
                }
                DisclosureGroup {
                    Text("• A coffee's taste comes from the library's ratings when there are enough, else from its tasting notes, process and roast (less sure).\n• With ratings shared, drinkers whose stars agree with yours count too.\n• Shown once 3 coffees are rated. Fixed rules on the controller; never touches the grind advice.")
                        .font(.footnote).foregroundStyle(palette.dim)
                } label: {
                    Text("Coffees you have not rated, ranked by how well they match your taste.")
                        .font(.footnote).foregroundStyle(palette.dim)
                }
            } else {
                DiscoverEmptyNote(text: "Recommendations cannot be read from the controller.",
                                  identifier: "discoverCoffeeRecommendationsEmpty")
            }
        }
    }
}

struct DiscoverSubscriptionRecommendationsCard: View {
    @Bindable var store: DiscoverStore
    @FocusState private var countryFocused: Bool

    var body: some View {
        let palette = BrewTheme.shared.palette
        let recs = store.subscriptionRecommendations
        let basis: String = {
            guard let recs else { return "" }
            if recs.basis == "taste" { return "By how their coffees taste to you" }
            if recs.basis == "reviews" {
                return "By reviews and what matters to you, until you have rated " + discoverMoreCoffees(recs.coffees)
            }
            return ""
        }()
        DiscoverCard(title: "Subscriptions you might like", subtitle: basis, identifier: "discoverSubscriptionRecommendations") {
            HStack {
                Text("Your country").font(.subheadline).foregroundStyle(palette.text)
                Spacer()
                TextField("e.g. US", text: $store.country)
                    .textInputAutocapitalization(.characters).autocorrectionDisabled()
                    .multilineTextAlignment(.trailing)
                    .frame(width: 70)
                    .textFieldStyle(.roundedBorder)
                    .focused($countryFocused)
                    .onSubmit { Task { await store.savePrefs() } }
                    .onChange(of: countryFocused) { _, focused in
                        if !focused { Task { await store.savePrefs() } }
                    }
                    .onChange(of: store.country) { _, text in if text.count > 2 { store.country = String(text.prefix(2)) } }
                    .accessibilityIdentifier("discoverCountry")
            }
            Text("What matters to you").font(.footnote).foregroundStyle(palette.dim)
            DiscoverMatterChips(selected: store.matters) { key in Task { await store.toggleMatter(key) } }
            if let recs, !recs.items.isEmpty {
                ForEach(recs.items) { item in
                    SubscriptionLibraryCard(service: item.service, why: item.why, people: item.people,
                                            have: store.hasService(item.service)) {
                        Task { await store.add(item.service) }
                    }
                }
            } else {
                DiscoverEmptyNote(
                    text: store.services.isEmpty
                        ? "Suggestions come from the shared library's services, how their coffees taste to people like you, and their reviews. The library has none listed yet, or it cannot be reached from here."
                        : "Nothing to suggest yet: services you already have are left out, and so are those that do not ship to your country.",
                    identifier: "discoverSubscriptionRecommendationsEmpty")
            }
        }
    }
}

/// The sourcing claims as chips that toggle.
struct DiscoverMatterChips: View {
    let selected: [String]
    let onToggle: (String) -> Void

    var body: some View {
        let palette = BrewTheme.shared.palette
        SubscriptionFlowLayout(spacing: 6) {
            ForEach(SubscriptionWording.sourcingKeys, id: \.self) { key in
                let on = selected.contains(key)
                Button { onToggle(key) } label: {
                    Text(SubscriptionWording.sourcing[key] ?? key)
                        .font(.footnote)
                        .padding(.horizontal, 10).padding(.vertical, 5)
                        .background(on ? palette.accent.opacity(0.85) : palette.track.opacity(0.6), in: Capsule())
                        .foregroundStyle(on ? Color.white : palette.text)
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(on ? .isSelected : [])
                .accessibilityIdentifier("discoverMatter.\(key)")
            }
        }
    }
}

/// A coffee of the shelf, opened from a recommendation (the Coffees page's own form, with its own store).
struct DiscoverCoffeeDestination: View {
    let coffeeId: Int
    @State private var store = CoffeeStore()

    var body: some View {
        Group {
            if store.coffee(id: coffeeId) != nil {
                CoffeeDetailView(store: store, coffeeId: coffeeId, draftStart: nil, onFinished: nil)
            } else {
                ProgressView().frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .task { await store.load() }
    }
}
