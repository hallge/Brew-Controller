// What the Subscriptions screen works on: the list, the library (coffees and bags, for the arrival
// dialog), the shared library's services, the reviews and the drinker names; and the actions on them.
import Observation
import SwiftUI

@MainActor
@Observable
final class SubscriptionsStore {
    enum Phase: Equatable {
        case loading
        case loaded
        case failed(String)
    }

    private(set) var phase: Phase = .loading
    private(set) var list = SubscriptionList()
    private(set) var library = LibraryModel()
    private(set) var services: [SubscriptionLibraryService] = []
    private(set) var reviews: [SubscriptionReview] = []
    private(set) var drinkers = InsightsRatings()
    /// What the last action said, for a banner (nil: nothing to say).
    var notice: SubscriptionNotice?

    @ObservationIgnored private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Loading

    func load() async {
        if case .failed = phase { phase = .loading }
        async let listResult = fetch(SubscriptionList.self, "/api/subscriptions")
        async let libraryResult = fetch(LibraryModel.self, "/api/library")
        async let servicesResult = fetch(SubscriptionLibraryList.self, "/api/subscriptions/library")
        async let reviewsResult = fetch(SubscriptionReviewList.self, "/api/subscriptions/reviews")
        async let drinkersResult = fetch(InsightsRatings.self, "/api/coffee/ratings")
        let loadedList = await listResult
        if let library = await libraryResult { self.library = library }
        services = (await servicesResult)?.services ?? []
        reviews = (await reviewsResult)?.reviews ?? []
        if let drinkers = await drinkersResult { self.drinkers = drinkers }
        if let loadedList {
            list = loadedList
            phase = .loaded
        } else if case .loading = phase {
            phase = .failed("Could not load the subscriptions.")
        }
    }

    /// Only the subscriptions and the library (after an action).
    func reload() async {
        async let listResult = fetch(SubscriptionList.self, "/api/subscriptions")
        async let libraryResult = fetch(LibraryModel.self, "/api/library")
        if let loadedList = await listResult { list = loadedList }
        if let library = await libraryResult { self.library = library }
    }

    func reloadReviews() async {
        reviews = (await fetch(SubscriptionReviewList.self, "/api/subscriptions/reviews"))?.reviews ?? reviews
    }

    private func fetch<T: Decodable & Sendable>(_ type: T.Type, _ path: String) async -> T? {
        try? await api.get(path, as: type)
    }

    // MARK: Derived

    var active: [SubscriptionItem] { list.subscriptions.filter { $0.isActive } }

    /// Active ones first, then by days until the next date (those without a date last).
    var ordered: [SubscriptionItem] {
        list.subscriptions.sorted { a, b in
            if a.isActive != b.isActive { return a.isActive }
            switch (a.daysUntil, b.daysUntil) {
            case let (x?, y?): return x < y
            case (nil, _?): return false
            case (_?, nil): return true
            default: return a.id < b.id
            }
        }
    }

    var nextUp: SubscriptionItem? {
        active.filter { $0.daysUntil != nil }.min { ($0.daysUntil ?? 0) < ($1.daysUntil ?? 0) }
    }

    var supplies: [SubscriptionItem] { list.subscriptions.filter { $0.kind == "supplies" && $0.perShot > 0 } }

    var reminders: [SubscriptionItem] { active.filter { $0.dueSoon || $0.stockShort } }

    /// Bags on their way from this subscription (or from none).
    func bagsOnTheWay(for item: SubscriptionItem) -> [BagModel] {
        library.bags.filter { $0.onTheWay && $0.subscriptionId == item.id }
    }

    func coffeeName(_ id: Int) -> String? { library.coffee(id: id)?.name }

    func hasLibraryService(_ service: SubscriptionLibraryService) -> Bool {
        list.subscriptions.contains { $0.libraryId == service.id || (!host($0.url).isEmpty && host($0.url) == host(service.url)) }
    }

    private func host(_ url: String) -> String {
        guard let host = URL(string: url)?.host else { return "" }
        return host.hasPrefix("www.") ? String(host.dropFirst(4)) : host
    }

    func reviews(for subscription: Int) -> [SubscriptionReview] {
        reviews.filter { $0.subscription == subscription }
    }

    // MARK: Actions

    /// Saves a subscription (id 0 adds). Returns the id, or nil after telling why.
    @discardableResult
    func save(_ item: SubscriptionItem) async -> Int? {
        do {
            let answer = try await api.post("/api/subscriptions", json: item.json, as: SubscriptionSaved.self)
            await reload()
            return answer.id
        } catch {
            say(error)
            return nil
        }
    }

    func delete(_ item: SubscriptionItem) async -> Bool {
        await run("/api/subscriptions/delete", ["id": .int(item.id)], done: "Deleted")
    }

    func markArrived(_ item: SubscriptionItem) async -> Bool {
        await run("/api/subscriptions/arrived", ["id": .int(item.id)], done: "Arrived")
    }

    func skip(_ item: SubscriptionItem) async -> Bool {
        await run("/api/subscriptions/skip", ["id": .int(item.id)], done: "Moved on to the next date")
    }

    func share(_ item: SubscriptionItem) async -> Bool {
        await run("/api/subscriptions/share", ["id": .int(item.id)], done: "Sent for review")
    }

    func add(_ service: SubscriptionLibraryService) async -> Int? {
        do {
            let answer = try await api.post("/api/subscriptions", json: service.addJSON, as: SubscriptionSaved.self)
            await reload()
            notice = SubscriptionNotice(text: service.name + " added: set its next date and price", good: true)
            return answer.id
        } catch {
            say(error)
            return nil
        }
    }

    /// Saves a review; a drinker has one review per subscription.
    func saveReview(subscription: Int, drinker: Int, stars: Int, aspects: [String: Int], note: String) async -> Bool {
        guard stars > 0 else {
            notice = SubscriptionNotice(text: "Give it 1 to 5 stars", good: false)
            return false
        }
        var given: [String: BrewJSON] = [:]
        for (key, value) in aspects where value > 0 { given[key] = .int(value) }
        let ok = await run("/api/subscriptions/review",
                           ["subscription": .int(subscription), "drinker": .int(drinker), "stars": .int(stars),
                            "aspects": .object(given), "note": .string(note)], done: "Review saved", reloadList: false)
        if ok { await reloadReviews() }
        return ok
    }

    func deleteReview(_ review: SubscriptionReview) async -> Bool {
        let ok = await run("/api/subscriptions/review/delete", ["id": .int(review.id)], done: "Deleted", reloadList: false)
        if ok { await reloadReviews() }
        return ok
    }

    /// Reads a shop's page: POST the link, then poll until the controller has an answer (up to 20 s).
    func readPage(_ url: String) async -> SubscriptionPreview? {
        guard url.lowercased().hasPrefix("http://") || url.lowercased().hasPrefix("https://") else {
            notice = SubscriptionNotice(text: "Paste a link starting https://", good: false)
            return nil
        }
        do {
            try await api.post("/api/subscriptions/preview", json: ["url": .string(url)])
        } catch {
            say(error)
            return nil
        }
        for _ in 0..<20 {
            try? await Task.sleep(for: .seconds(1))
            if Task.isCancelled { return nil }
            guard let preview = await fetch(SubscriptionPreview.self, "/api/subscriptions/preview") else { continue }
            if preview.url == url && preview.state != "waiting" && preview.state != "none" { return preview }
        }
        return SubscriptionPreview(state: "waiting", url: url)
    }

    // MARK: The arrival dialog

    /// What the owner picked for a delivery of coffee.
    enum ArrivalChoice: Hashable {
        /// A bag on its way (the bag id).
        case onTheWay(Int)
        /// A new coffee.
        case newCoffee
        /// A coffee on the shelf (the coffee id).
        case shelf(Int)
    }

    /// Coffees a delivery can be put with: those on the shelf, or with an unopened bag waiting.
    var shelfCoffees: [CoffeeModel] {
        library.coffees.filter { coffee in
            !coffee.isFinished || library.bags.contains { $0.coffeeId == coffee.id && $0.unopened }
        }
    }

    func onTheWayBags(for item: SubscriptionItem) -> [BagModel] {
        library.bags.filter { $0.onTheWay && ($0.subscriptionId == item.id || $0.subscriptionId == 0) }
    }

    /// Puts a delivery on the shelf, unopened, with the roast date printed on the bag, and marks the
    /// subscription arrived (web page subArrSave). `roastDate` is YYYYMMDD.
    func arrive(_ item: SubscriptionItem, choice: ArrivalChoice, roastDate: Int, weightG: Int,
                newName: String, newRoaster: String, newNotes: String) async -> Bool {
        guard roastDate > 0 else {
            notice = SubscriptionNotice(text: "Enter the roast date printed on the bag", good: false)
            return false
        }
        guard roastDate <= list.today || list.today == 0 else {
            notice = SubscriptionNotice(text: "That date has not come yet", good: false)
            return false
        }
        do {
            switch choice {
            case .onTheWay(let bagId):
                try await api.post("/api/library/do", query: ["what": "bag_arrived", "a": String(bagId), "b": "0"])
                let before = library.bags.first { $0.id == bagId }
                try await api.post("/api/bag", json: [
                    "id": .int(bagId), "roastDate": .int(roastDate), "openedDate": .int(0), "frozenDate": .int(0),
                    "thawDate": .int(0), "weightG": .int(weightG > 0 ? weightG : (before?.weightG ?? 0)),
                ])
            case .newCoffee, .shelf:
                var coffeeId = 0
                if case .shelf(let id) = choice { coffeeId = id }
                if coffeeId == 0 {
                    let name = newName.trimmingCharacters(in: .whitespaces)
                    guard !name.isEmpty else {
                        notice = SubscriptionNotice(text: "Give the new coffee a name", good: false)
                        return false
                    }
                    let made = try await api.post("/api/coffee", json: [
                        "name": .string(name), "roaster": .string(newRoaster.trimmingCharacters(in: .whitespaces)),
                        "notes": .string(newNotes.trimmingCharacters(in: .whitespaces)),
                    ], as: SubscriptionSaved.self)
                    coffeeId = made.id
                }
                try await api.post("/api/bag", json: [
                    "coffeeId": .int(coffeeId), "roastDate": .int(roastDate), "weightG": .int(weightG),
                    "unopened": .bool(true), "subscriptionId": .int(item.id),
                ])
            }
            try await api.post("/api/subscriptions/arrived", json: ["id": .int(item.id)])
            notice = SubscriptionNotice(text: "On the shelf, unopened", good: true)
            await reload()
            return true
        } catch {
            say(error)
            await reload()
            return false
        }
    }

    // MARK: Helpers

    @discardableResult
    private func run(_ path: String, _ body: [String: BrewJSON], done: String, reloadList: Bool = true) async -> Bool {
        do {
            try await api.post(path, json: body)
            notice = SubscriptionNotice(text: done, good: true)
            if reloadList { await reload() }
            return true
        } catch {
            say(error)
            return false
        }
    }

    private func say(_ error: Error) {
        let words = (error as? BrewAPIError)?.message ?? String(describing: error)
        notice = SubscriptionNotice(text: words, good: false)
    }
}

/// `{ok, id}` of a save.
struct SubscriptionSaved: Codable, Sendable, Equatable {
    var id = 0

    enum CodingKeys: String, CodingKey { case id }
}

extension SubscriptionSaved {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
    }
}

/// A line for the banner at the top of the screen.
struct SubscriptionNotice: Equatable, Identifiable {
    var text: String
    var good: Bool
    var id = UUID()
}
