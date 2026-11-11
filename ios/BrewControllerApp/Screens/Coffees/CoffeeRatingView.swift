// "How you liked it" (W: #crate): who is rating when named drinkers are on, 1 to 5 stars, five attribute
// sliders, eight flavour families, save, and the list of this coffee's ratings with edit and delete. It
// rates the coffee's bag, not one shot (shots are rated on the Shots screen).
import SwiftUI

/// A rating as it is being edited.
struct CoffeeRatingDraft: Equatable {
    /// 0: a new rating.
    var id = 0
    var drinker = 0
    var stars = 0
    var attr = [0, 0, 0, 0, 0]
    var flavours = 0
    /// The bag the rating is for: the coffee's bag in use, or the bag of the rating being changed.
    var bag = 0
}

struct CoffeeRatingSection: View {
    let store: CoffeeStore
    let coffee: CoffeeModel

    @State private var draft = CoffeeRatingDraft()
    @State private var preparedFor: String?
    @State private var ratingToDelete: Int?

    private var ratings: CoffeeRatingsModel? { store.ratings }

    var body: some View {
        if ratings == nil {
            Section {
                if store.ratingsTried {
                    Text("Ratings are unavailable: the controller did not answer.")
                        .font(.footnote).foregroundStyle(.secondary)
                        .accessibilityIdentifier("coffeeRatingsUnavailable")
                    Button("Retry") { Task { await store.loadRatings() } }
                        .accessibilityIdentifier("coffeeRatingsRetry")
                } else {
                    HStack { ProgressView(); Text("Loading ratings...").foregroundStyle(.secondary) }
                }
            } header: {
                Text("How you liked it")
            }
        }
        if let ratings {
            Group {
              Section {
                content(ratings)
              } header: {
                HStack {
                    Text("How you liked it")
                    Spacer()
                    if let bag = store.library?.bags.first(where: { $0.id == coffee.bagId }), bag.roastDate != 0 {
                        Text("the bag roasted " + CoffeeDates.text(bag.roastDate)).textCase(nil)
                    }
                }
              } footer: {
                Text("For the coffee, not one shot: how you liked this bag. Rate again to change it. Stars are needed; sliders and flavours are optional (0 means not given). Ratings build your taste on Insights and the coffees recommended to you.")
              }
            }
            .onAppear { prepare(ratings, force: false) }
            .onChange(of: key(ratings)) { _, _ in prepare(ratings, force: false) }
            .confirmationDialog("Delete this rating?", isPresented: Binding(
                get: { ratingToDelete != nil }, set: { if !$0 { ratingToDelete = nil } }
            ), titleVisibility: .visible) {
                Button("Delete", role: .destructive) {
                    if let id = ratingToDelete {
                        Task {
                            await store.deleteRating(id)
                            if draft.id == id { prepare(store.ratings ?? ratings, force: true) }
                        }
                    }
                    ratingToDelete = nil
                }
            }
        }
    }

    // MARK: Content

    @ViewBuilder
    private func content(_ ratings: CoffeeRatingsModel) -> some View {
        if ratings.named {
            if ratings.people.isEmpty {
                Text("Named drinkers is on, but no names are set: add them in Setup.")
                    .font(.footnote).foregroundStyle(.secondary)
            } else {
                Picker("Who is rating", selection: Binding(
                    get: { draft.drinker }, set: { pick(drinker: $0, ratings) }
                )) {
                    ForEach(ratings.people, id: \.id) { person in
                        Text(person.name).tag(person.id)
                    }
                }
                .accessibilityIdentifier("coffeeRatingWho")
            }
        }

        HStack {
            ForEach(1...5, id: \.self) { star in
                Button {
                    draft.stars = star
                } label: {
                    Image(systemName: star <= draft.stars ? "star.fill" : "star")
                        .font(.title2)
                        .foregroundStyle(star <= draft.stars ? BrewTheme.shared.palette.accent : Color.secondary)
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("\(star) star" + (star > 1 ? "s" : ""))
                .accessibilityIdentifier("coffeeRatingStar-\(star)")
            }
            if draft.stars == 0 { Text("tap to rate").font(.footnote).foregroundStyle(.secondary) }
        }

        ForEach(0..<CoffeeRatingText.attributes.count, id: \.self) { index in
            VStack(alignment: .leading) {
                HStack {
                    Text(CoffeeRatingText.attributes[index])
                    Spacer()
                    Text(draft.attr[index] > 0 ? "\(draft.attr[index]) / 5" : "not given").foregroundStyle(.secondary)
                }
                Slider(value: Binding(get: { Double(draft.attr[index]) }, set: { draft.attr[index] = Int($0.rounded()) }),
                       in: 0...5, step: 1)
                    .accessibilityIdentifier("coffeeRatingAttr-\(index)")
            }
        }

        VStack(alignment: .leading, spacing: 6) {
            Text("Flavours you tasted").font(.subheadline).foregroundStyle(.secondary)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 130), spacing: 8)], alignment: .leading, spacing: 8) {
                ForEach(0..<CoffeeRatingText.families.count, id: \.self) { index in
                    let on = draft.flavours & (1 << index) != 0
                    Button {
                        draft.flavours ^= (1 << index)
                    } label: {
                        Text(CoffeeRatingText.families[index])
                            .font(.footnote)
                            .padding(.horizontal, 10).padding(.vertical, 6)
                            .frame(maxWidth: .infinity)
                            .background(on ? BrewTheme.shared.palette.accent.opacity(0.25) : Color.secondary.opacity(0.12),
                                        in: Capsule())
                    }
                    .buttonStyle(.plain)
                    .accessibilityAddTraits(on ? .isSelected : [])
                    .accessibilityIdentifier("coffeeRatingFlavour-\(index)")
                }
            }
        }

        HStack {
            Button("Save rating") { Task { await save(ratings) } }
                .accessibilityIdentifier("coffeeRatingSave")
            Spacer()
            Button("Clear") { clear(ratings) }
                .accessibilityIdentifier("coffeeRatingClear")
        }
        .buttonStyle(.borderless)
        if draft.id != 0 {
            Text("Changing your rating of this bag" + (ratings.named ? " (\(ratings.drinkerName(draft.drinker)))" : ""))
                .font(.footnote).foregroundStyle(.secondary)
        }

        let mine = ratings.ratings.filter { $0.coffee == coffee.id }
        if mine.isEmpty {
            Text("Not rated yet.").font(.footnote).foregroundStyle(.secondary)
        }
        ForEach(mine) { rating in
            VStack(alignment: .leading, spacing: 4) {
                HStack {
                    Text(String(repeating: "★", count: rating.stars)).foregroundStyle(BrewTheme.shared.palette.accent)
                    Text(who(rating, ratings)).font(.footnote)
                }
                Text(detail(rating)).font(.caption).foregroundStyle(.secondary)
                HStack {
                    Button("Edit") { edit(rating) }
                        .accessibilityIdentifier("coffeeRatingEdit-\(rating.id)")
                    Button("Delete", role: .destructive) { ratingToDelete = rating.id }
                        .accessibilityIdentifier("coffeeRatingDelete-\(rating.id)")
                }
                .buttonStyle(.borderless)
                .font(.subheadline)
            }
            .accessibilityElement(children: .contain)
            .accessibilityIdentifier("coffeeRating-\(rating.id)")
        }
    }

    private func who(_ rating: CoffeeRatingEntry, _ ratings: CoffeeRatingsModel) -> String {
        var parts: [String] = []
        if ratings.named || rating.drinker != 0 { parts.append(ratings.drinkerName(rating.drinker)) }
        if let date = BrewFormat.date(fromEpoch: rating.t) { parts.append(date.formatted(date: .abbreviated, time: .omitted)) }
        if let bag = store.library?.bags.first(where: { $0.id == rating.bag }), bag.roastDate != 0 {
            parts.append("bag roasted " + CoffeeDates.text(bag.roastDate))
        }
        return parts.joined(separator: " · ")
    }

    private func detail(_ rating: CoffeeRatingEntry) -> String {
        let attrs = rating.attr.enumerated().compactMap { index, value in
            value > 0 ? CoffeeRatingText.attributes[index].lowercased() + " \(value)" : nil
        }.joined(separator: ", ")
        let flavours = CoffeeRatingText.families.enumerated().compactMap { index, name in
            rating.flavours & (1 << index) != 0 ? name.lowercased() : nil
        }.joined(separator: ", ")
        var text = [attrs, flavours].filter { !$0.isEmpty }.joined(separator: " · ")
        if text.isEmpty { text = "stars only" }
        if rating.share != "local" { text += " · shared: " + rating.share }
        return text
    }

    // MARK: Editing

    /// What changes the starting point of the form: the coffee, its bag, and the ratings stored.
    private func key(_ ratings: CoffeeRatingsModel) -> String {
        "\(coffee.id)-\(coffee.bagId)-\(ratings.ratings.count)-\(ratings.named)"
    }

    /// Starts the form from the drinker's existing rating of the bag in use, or an empty one.
    private func prepare(_ ratings: CoffeeRatingsModel, force: Bool) {
        let token = key(ratings)
        guard force || preparedFor != token else { return }
        preparedFor = token
        let drinker: Int
        if ratings.named, let first = ratings.people.first {
            let keep = ratings.people.contains { $0.id == draft.drinker }
            drinker = keep ? draft.drinker : first.id
        } else {
            drinker = 0
        }
        draft = start(for: drinker, ratings)
    }

    private func start(for drinker: Int, _ ratings: CoffeeRatingsModel) -> CoffeeRatingDraft {
        if let old = ratings.rating(coffee: coffee.id, bag: coffee.bagId, drinker: drinker) {
            return CoffeeRatingDraft(id: old.id, drinker: drinker, stars: old.stars, attr: old.attr,
                                     flavours: old.flavours, bag: old.bag)
        }
        return CoffeeRatingDraft(id: 0, drinker: drinker, stars: 0, attr: [0, 0, 0, 0, 0], flavours: 0, bag: coffee.bagId)
    }

    private func pick(drinker: Int, _ ratings: CoffeeRatingsModel) {
        draft = start(for: drinker, ratings)
    }

    private func edit(_ rating: CoffeeRatingEntry) {
        draft = CoffeeRatingDraft(id: rating.id, drinker: rating.drinker, stars: rating.stars, attr: rating.attr,
                                  flavours: rating.flavours, bag: rating.bag)
    }

    private func clear(_ ratings: CoffeeRatingsModel) {
        draft = CoffeeRatingDraft(id: 0, drinker: draft.drinker, stars: 0, attr: [0, 0, 0, 0, 0], flavours: 0,
                                  bag: coffee.bagId)
    }

    private func save(_ ratings: CoffeeRatingsModel) async {
        let saved = await store.saveRating(id: draft.id, coffee: coffee.id, bag: draft.bag, drinker: draft.drinker,
                                           stars: draft.stars, attr: draft.attr, flavours: draft.flavours)
        guard saved, let fresh = store.ratings else { return }
        // The rating now exists: keep editing it (a second Save changes it instead of adding another).
        if let mine = fresh.rating(coffee: coffee.id, bag: draft.bag, drinker: draft.drinker) {
            draft.id = mine.id
        }
    }
}
