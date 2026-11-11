// The Community page's information cards: trending recipes, what other owners settled on for what you
// are brewing, the shared grinder list and what you share.
import SwiftUI

struct CommunityTrendingCard: View {
    let store: CommunityStore
    let onMake: (DiscoverRecipeDraft) -> Void

    var body: some View {
        let palette = BrewTheme.shared.palette
        let recipes = store.trending?.recipes ?? []
        CommunityCard(title: "Trending recipes",
                      subtitle: (store.trending?.days ?? 0) > 0 ? "the last \(store.trending?.days ?? 0) days" : "",
                      identifier: "communityTrending") {
            if recipes.isEmpty {
                CommunityEmptyNote(
                    text: "Coming as more people share: the recipes owners settled on lately, once at least two people settled on the same one.",
                    identifier: "communityTrendingEmpty")
            } else {
                ForEach(Array(recipes.enumerated()), id: \.element.id) { entry in
                    let recipe = entry.element
                    VStack(alignment: .leading, spacing: 3) {
                        Text("#\(entry.offset + 1)").font(.caption).foregroundStyle(palette.dim)
                        Text("\(BrewFormat.decimal(recipe.doseG, 0)) g in, \(BrewFormat.decimal(recipe.yieldG, 0)) g out")
                            .font(.headline).foregroundStyle(palette.text)
                        Text(CommunityText.trendingLine(recipe)).font(.subheadline).foregroundStyle(palette.text)
                        Text(CommunityText.trendingCounts(recipe)).font(.footnote).foregroundStyle(palette.dim)
                        Button("Make it a recipe") {
                            onMake(DiscoverRecipeDraft(
                                name: "\(BrewFormat.decimal(recipe.doseG, 0)) g in, \(BrewFormat.decimal(recipe.yieldG, 0)) g out",
                                doseG: recipe.doseG, yieldG: recipe.yieldG, timeS: recipe.timeS.rounded()))
                        }
                        .buttonStyle(.bordered)
                        .font(.footnote)
                        .padding(.top, 4)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(10)
                    .background(palette.track.opacity(0.4), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
                }
            }
        }
    }
}

struct CommunityNowCard: View {
    let store: CommunityStore
    @State private var confirmStart: Int?

    var body: some View {
        let palette = BrewTheme.shared.palette
        let coffee = store.activeCoffee
        CommunityCard(title: "For what you are brewing", subtitle: coffee?.name ?? "", identifier: "communityNow") {
            if let coffee {
                if store.brewingEntry == nil {
                    CommunityEmptyNote(text: noEntryText(coffee), identifier: "communityNowEmpty")
                } else if let now = store.now {
                    answer(coffee: coffee, now: now)
                } else {
                    CommunityEmptyNote(text: "What other owners settled on cannot be read from the controller right now.",
                                       identifier: "communityNowEmpty")
                }
            } else {
                CommunityEmptyNote(
                    text: "No coffee is attached to what is being brewed, so there is nothing from other people to show.",
                    identifier: "communityNowEmpty")
            }
        }
        .alert("Replace your setting?", isPresented: Binding(get: { confirmStart != nil }, set: { if !$0 { confirmStart = nil } })) {
            Button("Replace") {
                if let value = confirmStart { Task { await store.useStart(value) } }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            let dial = store.library.grinder.dial
            Text("Replace your setting of \(dial.setting(store.brewingEntry?.preset.grindUm ?? 0)) with \(dial.setting(confirmStart ?? 0))?")
        }
        .tint(palette.accent)
    }

    // MARK: Answers

    @ViewBuilder
    private func answer(coffee: CoffeeModel, now: CommunityNow) -> some View {
        let dial = store.library.grinder.dial
        if coffee.libraryId.isEmpty {
            CommunityEmptyNote(
                text: "\(coffee.name) is not in the shared library yet. Once it is published, where other owners settle shows here.",
                identifier: "communityNowEmpty")
        } else if !now.found {
            CommunityEmptyNote(text: notFoundText(coffee: coffee, now: now), identifier: "communityNowEmpty")
        } else {
            let setting = now.start.setting
            tile("Start your grinder at",
                 setting != 0 ? "\(dial.setting(setting)) ± \(dial.delta(now.start.range)) \(dial.deltaUnit)" : "not known",
                 CommunityText.startWhy(now, dial: dial))
            tile("What a good shot looked like", BrewFormat.decimal(now.timeS, 1) + " s", goodShotDetail)
            tile("Based on", "\(now.contributors) \(now.contributors == 1 ? "person" : "people")", basedOnDetail(now))
            if setting != 0 { useButton(setting) }
        }
        similar(now.similar, dial: dial)
        if !now.similar.found, !(now.found && now.start.setting != 0), let guess = now.rangeGuess, guess.setting != 0 {
            tile("A first guess from your grinder's range", "\(dial.setting(guess.setting)) ± \(dial.delta(guess.range)) \(dial.deltaUnit)",
                 "Nothing else is known for this coffee yet. Grind advice takes over from your first shot.")
                .accessibilityIdentifier("communityRangeGuess")
        }
    }

    @ViewBuilder
    private func similar(_ guess: CommunitySimilar, dial: BrewFormat.Dial) -> some View {
        if guess.found {
            tile("A guess from your own coffees", "\(dial.setting(guess.setting)) ± \(dial.delta(guess.range)) \(dial.deltaUnit)",
                 "From \(guess.fromCoffee) (\(dial.settingUnit(guess.fromSetting))), your nearest coffee"
                    + (guess.nearRecipe ? " on a close recipe" : "") + ". Grind advice takes over from your first shot.")
                .accessibilityIdentifier("communitySimilar")
            DisclosureGroup {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(CommunityText.similarLines(guess, dial: dial), id: \.self) { line in
                        Text("• " + line).font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            } label: {
                Text("How this guess is made").font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
            }
            if guess.setting != 0 { useButton(guess.setting) }
        }
    }

    private func useButton(_ setting: Int) -> some View {
        let dial = store.library.grinder.dial
        return VStack(alignment: .leading, spacing: 4) {
            Button("Use \(dial.setting(setting)) as my starting grind") {
                let current = store.brewingEntry?.preset.grindUm ?? 0
                if current != 0 && current != setting { confirmStart = setting } else { Task { await store.useStart(setting) } }
            }
            .buttonStyle(.borderedProminent)
            .accessibilityIdentifier("communityUseStart")
            Text(currentSettingText(dial: dial))
                .font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
        }
    }

    // MARK: Words

    private func noEntryText(_ coffee: CoffeeModel) -> String {
        coffee.libraryId.isEmpty
            ? "\(coffee.name) is not in the shared library yet. Once it is published, where other owners settle shows here."
            : "Tick a recipe for this coffee to see what others settled on for it."
    }

    private func notFoundText(coffee: CoffeeModel, now: CommunityNow) -> String {
        let recipe = store.activeRecipe
        let name = recipe?.name ?? "this one"
        let dose = BrewFormat.decimal(recipe?.doseG ?? 0, 0)
        let out = BrewFormat.decimal(recipe?.targetWeightG ?? 0, 0)
        var text = "Nobody has shared a settled dial-in for \(coffee.name) with a recipe like \(name) (\(dose) g in, \(out) g out) yet."
        if !now.reachable { text += " The shared library cannot be reached from this controller yet." }
        return text
    }

    private var goodShotDetail: String {
        guard let recipe = store.activeRecipe else { return "" }
        return "\(BrewFormat.decimal(recipe.doseG, 0)) g in, \(BrewFormat.decimal(recipe.targetWeightG, 0)) g out (\(recipe.name))"
    }

    private func basedOnDetail(_ now: CommunityNow) -> String {
        "\(now.dialins) settled dial-in\(now.dialins == 1 ? "" : "s"), within ± \(BrewFormat.decimal(now.spreadUm, 1)) µm of each other"
    }

    private func currentSettingText(dial: BrewFormat.Dial) -> String {
        let current = store.brewingEntry?.preset.grindUm ?? 0
        let tail = " A start only: grind advice takes it from there."
        if current != 0 { return "Your setting for it now: " + dial.settingUnit(current) + "." + tail }
        return "No setting entered for it yet." + tail
    }

    private func tile(_ title: String, _ value: String, _ detail: String) -> some View {
        InsightsTile(title: title, value: value, detail: detail)
    }
}

struct CommunityGrindersCard: View {
    let store: CommunityStore

    var body: some View {
        let palette = BrewTheme.shared.palette
        let grinders = store.grinders
        CommunityCard(title: "Grinders in the shared library", identifier: "communityGrinders") {
            if grinders.isEmpty {
                CommunityEmptyNote(text: "The shared grinder list cannot be read yet.", identifier: "communityGrindersEmpty")
            } else {
                Text("\(grinders.count) grinder models, each with how its setting reads and its range. Pick yours on the Grinder page; owners of the same model pool what they learn.")
                    .font(.subheadline).foregroundStyle(palette.text)
                Text(grinders.prefix(12).map { $0.model }.joined(separator: " · ")
                     + (grinders.count > 12 ? " and \(grinders.count - 12) more" : ""))
                    .font(.footnote).foregroundStyle(palette.dim)
                    .accessibilityIdentifier("communityGrinderList")
            }
            NavigationLink("Your grinder") { GrinderTab() }
                .accessibilityIdentifier("communityLinkGrinder")
        }
    }
}

struct CommunityShareCard: View {
    let store: CommunityStore

    var body: some View {
        let palette = BrewTheme.shared.palette
        CommunityCard(title: "What you share", subtitle: store.share == nil ? "" : (store.sharingOn ? "sharing" : "not sharing"),
                      identifier: "communityShare") {
            if let share = store.share {
                if !store.sharingOn {
                    CommunityEmptyNote(
                        text: "You are not sharing yet. Turn it on in Setup › Shared library to send your coffees, dial-ins and (if you choose) ratings. Nothing personal leaves the controller, and no account is needed.",
                        identifier: "communityShareOff")
                } else {
                    Text("\(share.sent) record\(share.sent == 1 ? "" : "s") sent, \(share.waiting) waiting.")
                        .font(.subheadline).foregroundStyle(palette.text)
                        .accessibilityIdentifier("communityShareCounts")
                    if !share.problem.isEmpty {
                        Text("Not reaching the library: \(share.problem).").font(.footnote).foregroundStyle(palette.accent)
                    } else if share.lastOk > 0 {
                        Text("Last reached " + Date(timeIntervalSince1970: TimeInterval(share.lastOk))
                            .formatted(date: .abbreviated, time: .shortened) + ".")
                            .font(.footnote).foregroundStyle(palette.dim)
                    }
                    Text("What each coffee shares, and what reviewers said, is on its page under Coffees.")
                        .font(.footnote).foregroundStyle(palette.dim)
                }
            } else {
                CommunityEmptyNote(text: "What you share cannot be read from the controller.", identifier: "communityShareEmpty")
            }
            HStack(spacing: 16) {
                NavigationLink("Setup › Shared library") { SetupScreen() }
                    .accessibilityIdentifier("communityLinkSetup")
                NavigationLink("Coffees") { CoffeesTab() }
                    .accessibilityIdentifier("communityLinkCoffees")
            }
            .font(.subheadline)
        }
    }
}
