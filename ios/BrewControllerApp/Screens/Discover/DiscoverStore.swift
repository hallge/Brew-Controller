// What the Discover screen shows and does: recommendations, the shared library's coffees and recipes,
// what waits for review, and your say about subscriptions (country, what matters).
import Observation
import SwiftUI

/// A line for the banner at the top of Discover.
struct DiscoverNotice: Equatable, Identifiable {
    var text: String
    var good: Bool
    var id = UUID()
}

/// A recipe from the recipe book or the trending list, to check and save ("Make it a recipe").
struct DiscoverRecipeDraft: Identifiable, Equatable {
    var name: String
    var doseG: Double
    var yieldG: Double
    var timeS: Double
    var holdS: Double = 0
    var bloomS: Double = 0
    var id = UUID()

    /// The body for POST /api/recipe: a new recipe (id 0) by weight, holding the pre-infusion when it has a hold.
    var json: [String: BrewJSON] {
        var body: [String: BrewJSON] = [
            "id": .int(0), "name": .string(String(name.prefix(23))), "doseG": .double(doseG),
            "useRatio": .bool(false), "targetWeightG": .double(yieldG),
        ]
        if timeS > 0 { body["targetTimeS"] = .double(timeS) }
        if holdS > 0 {
            body["preinfusion"] = .int(1)
            body["preinfusionHoldS"] = .double(holdS)
        }
        if bloomS > 0 { body["bloomPauseS"] = .double(bloomS) }
        return body
    }
}

@MainActor
@Observable
final class DiscoverStore {
    enum Phase: Equatable {
        case loading
        case loaded
    }

    private(set) var phase: Phase = .loading
    private(set) var ratings = InsightsRatings()
    /// 0 is the household, 1...8 a named drinker.
    var drinker = 0
    private(set) var recommendations: DiscoverRecommendations?
    private(set) var subscriptionRecommendations: SubscriptionRecommendations?
    private(set) var subscriptions = SubscriptionList()
    /// The shared library's services (empty when it cannot be reached).
    private(set) var services: [SubscriptionLibraryService] = []
    private(set) var library = LibraryModel()
    private(set) var coffees: [DiscoverLibraryCoffee] = []
    private(set) var recipes: [DiscoverRecipeEntry] = []
    /// nil: the controller could not be asked.
    private(set) var pending: DiscoverPending?
    var country = ""
    var matters: [String] = []
    var notice: DiscoverNotice?

    @ObservationIgnored private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Loading

    func load() async {
        async let ratingsResult = fetch(InsightsRatings.self, "/api/coffee/ratings")
        async let libraryResult = fetch(LibraryModel.self, "/api/library")
        async let subscriptionsResult = fetch(SubscriptionList.self, "/api/subscriptions")
        async let coffeesResult = fetch(DiscoverCoffeeIndex.self, "/api/community/coffees")
        async let recipesResult = fetch(DiscoverRecipeBook.self, "/api/community/recipes")
        async let pendingResult = fetch(DiscoverPending.self, "/api/library/pending")
        async let servicesResult = fetch(SubscriptionLibraryList.self, "/api/subscriptions/library")
        if let ratings = await ratingsResult { self.ratings = ratings }
        if let library = await libraryResult { self.library = library }
        if let subscriptions = await subscriptionsResult {
            self.subscriptions = subscriptions
            country = subscriptions.country
            matters = subscriptions.matters
        }
        coffees = (await coffeesResult)?.coffees ?? []
        recipes = (await recipesResult)?.items ?? []
        pending = await pendingResult
        services = (await servicesResult)?.services ?? []
        if !ratings.people.contains(where: { $0.id == drinker }) { drinker = 0 }
        await loadRecommendations()
        phase = .loaded
    }

    /// The two recommendation lists of the chosen drinker.
    func loadRecommendations() async {
        let query = ["drinker": String(drinker)]
        async let coffeeRecs = fetch(DiscoverRecommendations.self, "/api/taste/recommend", query: query)
        async let serviceRecs = fetch(SubscriptionRecommendations.self, "/api/subscriptions/recommend", query: query)
        recommendations = await coffeeRecs
        subscriptionRecommendations = await serviceRecs
    }

    func chooseDrinker(_ id: Int) async {
        drinker = id
        await loadRecommendations()
    }

    private func fetch<T: Decodable & Sendable>(_ type: T.Type, _ path: String,
                                                query: [String: String] = [:]) async -> T? {
        try? await api.get(path, query: query, as: type)
    }

    // MARK: Derived

    func coffeeName(_ id: Int) -> String? { library.coffee(id: id)?.name }

    /// The library ids of the coffees on the shelf.
    var haveLibraryIds: Set<String> { Set(library.coffees.map { $0.libraryId }.filter { !$0.isEmpty }) }

    var pendingCoffees: [DiscoverPendingItem] { (pending?.index.items ?? []).filter { $0.type == "coffee" } }
    var pendingRecipes: [DiscoverPendingItem] { (pending?.index.items ?? []).filter { $0.type == "recipe" } }

    /// Words above the waiting list.
    func pendingWhy(recipes: Bool) -> String {
        guard let pending else { return "The controller cannot be reached." }
        if !pending.linked { return "Link the shared library in Setup to see what is waiting." }
        if pending.canReview {
            return recipes ? "Pull it once if you can, then approve or decline." : "Check the details against the bag, then approve or decline."
        }
        return "You can review once something you shared has been published."
    }

    func hasService(_ service: SubscriptionLibraryService) -> Bool {
        subscriptions.subscriptions.contains { $0.libraryId == service.id }
    }

    // MARK: Actions

    func adopt(_ coffee: DiscoverLibraryCoffee) async {
        do {
            try await api.post("/api/coffee/adopt", json: coffee.adoptJSON)
            say("Added to your shelf", good: true)
            library = (try? await api.library()) ?? library
        } catch {
            say(error)
        }
    }

    /// Approve (note optional) or decline (a note is needed) an item waiting for review.
    func review(_ item: DiscoverPendingItem, approve: Bool, note: String) async -> Bool {
        let trimmed = note.trimmingCharacters(in: .whitespaces)
        if !approve && trimmed.isEmpty {
            say("Say why it is declined", good: false)
            return false
        }
        do {
            try await api.post("/api/library/review", json: [
                "target": .string(item.id), "verdict": .string(approve ? "approve" : "decline"),
                "note": .string(trimmed),
            ])
            say(approve ? "Approved: sent with your key" : "Declined: sent with your note", good: true)
            pending = await fetch(DiscoverPending.self, "/api/library/pending") ?? pending
            return true
        } catch {
            say(error)
            return false
        }
    }

    func makeRecipe(_ draft: DiscoverRecipeDraft) async -> Bool {
        do {
            try await api.post("/api/recipe", json: draft.json)
            say("Recipe saved", good: true)
            return true
        } catch {
            say(error)
            return false
        }
    }

    /// Saves the country and what matters, then asks for recommendations again.
    func savePrefs() async {
        let code = String(country.trimmingCharacters(in: .whitespaces).uppercased().prefix(2))
        country = code
        do {
            try await api.post("/api/subscriptions/prefs", json: [
                "country": .string(code), "matters": .array(matters.map { BrewJSON.string($0) }),
            ])
            await loadRecommendations()
        } catch {
            say(error)
        }
    }

    func toggleMatter(_ key: String) async {
        if matters.contains(key) { matters.removeAll { $0 == key } } else { matters.append(key) }
        await savePrefs()
    }

    /// Adds a recommended or library service to the owner's subscriptions.
    func add(_ service: SubscriptionLibraryService) async {
        do {
            try await api.post("/api/subscriptions", json: service.addJSON)
            say(service.name + " added: set its next date and price on Subscriptions", good: true)
            subscriptions = (try? await api.get("/api/subscriptions", as: SubscriptionList.self)) ?? subscriptions
        } catch {
            say(error)
        }
    }

    private func say(_ text: String, good: Bool) {
        notice = DiscoverNotice(text: text, good: good)
    }

    private func say(_ error: Error) {
        say((error as? BrewAPIError)?.message ?? String(describing: error), good: false)
    }
}
