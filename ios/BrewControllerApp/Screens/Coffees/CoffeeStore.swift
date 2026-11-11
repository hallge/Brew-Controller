// The Coffees screens' state: the shelf (the library), bags, ratings and the shared library, and every change
// through the board's own routes (src/net/Web.cpp). After a change the library is read again so the screen
// shows what the controller now has.
import Foundation
import Observation

@MainActor
@Observable
final class CoffeeStore {
    private(set) var library: LibraryModel?
    private(set) var ratings: CoffeeRatingsModel?
    private(set) var share: CoffeeShareModel?
    /// The ratings were asked for (they may still be nil: the screen then says they are unavailable).
    private(set) var ratingsTried = false
    /// Setup: sharing is switched on.
    private(set) var shareEnabled = false
    /// The coffees published in the shared library (GET /api/community/coffees).
    private(set) var sharedLibrary: [CoffeeLibraryEntry] = []
    private(set) var isLoading = false
    var errorText: String?
    private(set) var notice: String?

    @ObservationIgnored private let api: BrewAPI
    @ObservationIgnored private var noticeTask: Task<Void, Never>?

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Derived

    var coffees: [CoffeeModel] { library?.coffees ?? [] }
    var recipes: [RecipeDefModel] { library?.recipes ?? [] }
    var dial: BrewFormat.Dial { library?.grinder.dial ?? .microns }

    func coffee(id: Int) -> CoffeeModel? { coffees.first { $0.id == id } }

    func bags(of coffeeId: Int) -> [BagModel] {
        (library?.bags ?? []).filter { $0.coffeeId == coffeeId }.sorted { $0.id > $1.id }
    }

    /// The unopened bag a coffee would open next (oldest roast first).
    func waitingBag(_ coffee: CoffeeModel) -> BagModel? {
        (library?.bags ?? []).filter { $0.coffeeId == coffee.id && $0.unopened }
            .sorted { ($0.roastDate == 0 ? Int.max : $0.roastDate) < ($1.roastDate == 0 ? Int.max : $1.roastDate) }
            .first
    }

    private func matches(_ coffee: CoffeeModel, _ query: String) -> Bool {
        let text = query.trimmingCharacters(in: .whitespaces).lowercased()
        guard !text.isEmpty else { return true }
        return [coffee.name, coffee.roaster, coffee.origin, coffee.region, coffee.process, coffee.varietal]
            .joined(separator: " ").lowercased().contains(text)
    }

    /// Coffees on the shelf: not finished, or with an unopened bag waiting.
    func shelf(matching query: String) -> [CoffeeModel] {
        coffees.filter { ($0.finished == 0 || waitingBag($0) != nil) && matches($0, query) }
    }

    /// Coffees whose bag is finished, folded away but kept.
    func finished(matching query: String) -> [CoffeeModel] {
        coffees.filter { $0.finished != 0 && waitingBag($0) == nil && matches($0, query) }
    }

    /// Up to 8 coffees of the shared library that are not on the shelf and match the search.
    func sharedMatches(_ query: String) -> [CoffeeLibraryEntry] {
        let have = Set(coffees.map(\.libraryId).filter { !$0.isEmpty })
        let text = query.trimmingCharacters(in: .whitespaces).lowercased()
        return Array(sharedLibrary.filter { entry in
            guard !have.contains(entry.id) else { return false }
            guard !text.isEmpty else { return true }
            return [entry.name, entry.roaster, entry.origin, entry.region, entry.process, entry.varietal]
                .joined(separator: " ").lowercased().contains(text)
        }.prefix(8))
    }

    /// "Portrait · 2 recipes · 12 days off roast · 1 unopened bag · ..." (WebPage.h `drawCoffeeList`).
    func subtitle(_ coffee: CoffeeModel) -> String {
        var parts: [String] = []
        if !coffee.roaster.isEmpty { parts.append(coffee.roaster) }
        let entries = (library?.presets ?? []).filter { $0.coffeeId == coffee.id }.count
        parts.append(entries == 0 ? "not on the controller" : "\(entries) \(entries == 1 ? "recipe" : "recipes")")
        if coffee.finished == 0, coffee.bagId != 0, let days = CoffeeDates.daysSince(coffee.roastDate) {
            parts.append("\(days) days off roast")
        }
        let unopened = bags(of: coffee.id).filter { $0.unopened }
        if !unopened.isEmpty {
            var text = unopened.count == 1 ? "1 unopened bag" : "\(unopened.count) unopened bags"
            if let waiting = waitingBag(coffee), waiting.roastDate != 0, coffee.finished != 0 || coffee.bagId == 0 {
                text += ", roasted " + CoffeeDates.text(waiting.roastDate)
            }
            parts.append(text)
        }
        if let onTheWay = bags(of: coffee.id).first(where: { $0.onTheWay }) {
            parts.append("on its way" + (onTheWay.expectedDate != 0 ? ", expected " + CoffeeDates.text(onTheWay.expectedDate) : ""))
        }
        if coffee.share != "local" {
            let text = (coffee.share == "sent" && !(share?.toLibrary ?? true)) ? "not sent" : CoffeeShareText.state(coffee.share)
            parts.append(text + (coffee.shareChanged ? ", changed since" : ""))
        }
        return parts.joined(separator: " · ")
    }

    /// "1 coffee on the shelf, 2 finished (room for 24 in all)."
    var countLine: String {
        let onShelf = coffees.filter { $0.finished == 0 }.count
        let done = coffees.count - onShelf
        var text = "\(onShelf) \(onShelf == 1 ? "coffee" : "coffees") on the shelf"
        if done > 0 { text += ", \(done) finished" }
        return text + " (room for \(library?.maxCoffees ?? 0) in all)."
    }

    /// Is there already a coffee with this name and roaster on the shelf?
    func isDuplicate(_ draft: CoffeeDraft) -> Bool {
        coffees.contains {
            $0.name.lowercased() == draft.name.lowercased() && $0.roaster.lowercased() == draft.roaster.lowercased()
        }
    }

    // MARK: Loading

    /// The library (the controller may still be starting: a few tries), then what the screens add to it.
    func load() async {
        isLoading = true
        defer { isLoading = false }
        var attempt = 0
        while true {
            attempt += 1
            do {
                library = try await api.library()
                errorText = nil
                break
            } catch let error as BrewAPIError {
                if attempt < 5, RetryingTask<Int>.isRetryable(error) {
                    try? await Task.sleep(for: .milliseconds(500))
                    continue
                }
                if library == nil { errorText = error.message }
                return
            } catch {
                return
            }
        }
        await loadExtras()
    }

    /// Ratings, sharing and the shared library: each is optional, the shelf shows without them.
    func loadExtras() async {
        await loadRatings()
        share = try? await api.get("/api/share", as: CoffeeShareModel.self)
        if let settings = try? await api.settings() { shareEnabled = settings.shareEnabled }
        if let shared = try? await api.get("/api/community/coffees", as: CoffeeSharedLibraryModel.self) {
            sharedLibrary = shared.coffees
        }
    }

    func loadRatings() async {
        ratings = try? await api.get("/api/coffee/ratings", as: CoffeeRatingsModel.self)
        ratingsTried = true
    }

    // MARK: Coffee and bags

    /// Saves a coffee (id 0: new). Returns the id, nil when the controller refused. "The bag in use is
    /// corrected by these dates."
    @discardableResult
    func save(_ draft: CoffeeDraft, id: Int) async -> Int? {
        guard !draft.clipped().name.isEmpty else {
            errorText = "A coffee needs a name"
            return nil
        }
        do {
            let reply = try await api.post("/api/coffee", json: draft.body(id: id), as: CoffeeIdReply.self)
            await load()
            announce("Coffee saved")
            return reply.id
        } catch {
            fail(error)
            return nil
        }
    }

    /// Starts a new bag of a coffee: back on the shelf, with a fresh run for grind advice.
    func startBag(coffee: Int, roastDate: Int, openedDate: Int) async -> Bool {
        do {
            try await api.post("/api/bag", json: ["coffeeId": .int(coffee), "roastDate": .int(roastDate),
                                                  "openedDate": .int(openedDate)])
            await load()
            announce("New bag started")
            return true
        } catch {
            fail(error)
            return false
        }
    }

    func finish(_ coffee: CoffeeModel) async {
        if await act("coffee_finish", a: coffee.id, b: 0) { announce(coffee.name + " moved to Finished bags") }
    }

    func delete(_ coffee: CoffeeModel) async -> Bool {
        await act("coffee_delete", a: coffee.id, b: 0)
    }

    func useBag(coffee: Int, bag: Int) async { _ = await act("bag_use", a: coffee, b: bag) }
    func forgetBag(_ bag: Int) async { _ = await act("bag_delete", a: bag, b: 0) }

    func bagArrived(_ bag: Int) async {
        if await act("bag_arrived", a: bag, b: 0) { announce("On the shelf, unopened: add its roast date") }
    }

    func openBag(_ bag: Int) async {
        if await act("bag_open", a: bag, b: 0) { announce("Bag opened: tracking starts now") }
    }

    /// Ticks a recipe for a coffee (a controller entry) or takes it off again.
    func setBrewWith(coffee: Int, recipe: Int, on: Bool) async {
        if on {
            _ = await act("preset_add", a: recipe, b: coffee)
        } else if let index = library?.presets.firstIndex(where: { $0.coffeeId == coffee && $0.recipeId == recipe }) {
            _ = await act("preset_delete", a: index, b: 0)
        }
    }

    /// Uses the community's start as the entry's grind setting (`preset_grind`, a = entry, b = steps).
    func useStart(entry: Int, setting: Int) async {
        if await act("preset_grind", a: entry, b: setting) { announce("Starting grind set") }
    }

    // MARK: Sharing and adopting

    func send(_ coffee: CoffeeModel) async {
        do {
            try await api.post("/api/share/coffee", query: ["id": String(coffee.id)])
            announce("Queued for the shared library")
            await load()
        } catch {
            fail(error)
        }
    }

    /// Adds a coffee of the shared library or the directory to the shelf. Returns the new coffee's id.
    @discardableResult
    func adopt(_ entry: CoffeeLibraryEntry) async -> Int? {
        var body = CoffeeDraft(entry).body(id: 0)
        body["libraryId"] = .string(entry.id)
        body.removeValue(forKey: "id")
        body.removeValue(forKey: "roastDate")
        body.removeValue(forKey: "frozenDate")
        body.removeValue(forKey: "thawDate")
        do {
            let reply = try await api.post("/api/coffee/adopt", json: body, as: CoffeeIdReply.self)
            await load()
            announce("Added to your shelf")
            return reply.id
        } catch {
            fail(error)
            return nil
        }
    }

    // MARK: Ratings

    /// Saves one drinker's rating of a coffee's bag (id 0: a new rating).
    func saveRating(id: Int, coffee: Int, bag: Int, drinker: Int, stars: Int, attr: [Int], flavours: Int) async -> Bool {
        guard stars >= 1 else {
            errorText = "Give it 1 to 5 stars first"
            return false
        }
        do {
            try await api.post("/api/coffee/rate", json: [
                "id": .int(id), "coffee": .int(coffee), "bag": .int(bag), "drinker": .int(drinker),
                "stars": .int(stars), "attr": .array(attr.map { .int($0) }), "flavours": .int(flavours),
            ])
            await loadRatings()
            announce("Rating saved")
            return true
        } catch {
            fail(error)
            return false
        }
    }

    func deleteRating(_ id: Int) async {
        do {
            try await api.post("/api/coffee/rate/delete", query: ["id": String(id)])
            await loadRatings()
            announce("Deleted")
        } catch {
            fail(error)
        }
    }

    // MARK: Community and directory

    /// What other owners settled on for a coffee with a recipe. nil when the controller cannot say.
    func community(coffee: Int, recipe: Int) async -> CoffeeCommunityModel? {
        try? await api.get("/api/community", query: ["coffee": String(coffee), "recipe": String(recipe)],
                           as: CoffeeCommunityModel.self)
    }

    func directory(query: String, origin: String, process: String, roast: String, page: Int) async throws
        -> CoffeeDirectoryModel {
        try await api.get("/api/directory", query: ["q": query, "origin": origin, "process": process,
                                                    "roast": roast, "page": String(page)],
                          as: CoffeeDirectoryModel.self)
    }

    // MARK: Beanconqueror import

    /// Adds the beans of a Beanconqueror backup that are not on the shelf yet; returns the sentence to show.
    func importBeanconqueror(_ data: Data, includeFinished: Bool) async -> String {
        guard let parsed = CoffeeBeanconqueror.parse(data, includeFinished: includeFinished) else {
            return "That is not a Beanconqueror backup file (it is not JSON)"
        }
        guard !parsed.beans.isEmpty else {
            return "No beans found in that file" + (parsed.skipped.isEmpty ? "" : " (\(parsed.skipped.count) skipped)")
        }
        var have = Set(coffees.map { ($0.name + "|" + $0.roaster).lowercased() })
        var added = 0, duplicates = 0, full = 0
        let room = library?.maxCoffees ?? 0
        for bean in parsed.beans {
            let key = (bean.coffee.name + "|" + bean.coffee.roaster).lowercased()
            if have.contains(key) {
                duplicates += 1
                continue
            }
            if coffees.count + added >= room {
                full += 1
                continue
            }
            guard let reply = try? await api.post("/api/coffee", json: bean.coffee.body(id: 0), as: CoffeeIdReply.self),
                  reply.id != 0 else { continue }
            added += 1
            have.insert(key)
            if bean.hasBagDetails, let fresh = try? await api.library(),
               let bagId = fresh.coffee(id: reply.id)?.bagId, bagId != 0 {
                _ = try? await api.post("/api/bag", json: [
                    "id": .int(bagId), "roastDate": .int(bean.coffee.roastDate), "openedDate": .int(bean.openedDate),
                    "frozenDate": .int(bean.frozenDate), "thawDate": .int(bean.thawDate), "weightG": .int(bean.weightG),
                ])
            }
        }
        await load()
        var text = "From Beanconqueror: \(added) coffee" + (added == 1 ? "" : "s") + " added"
        if duplicates > 0 { text += ", \(duplicates) already on the shelf" }
        if full > 0 { text += ", \(full) left out: the shelf is full" }
        if !parsed.skipped.isEmpty { text += ", \(parsed.skipped.count) skipped" }
        return text + ". Shots are not imported."
    }

    // MARK: Helpers

    private func act(_ what: String, a: Int, b: Int) async -> Bool {
        do {
            try await api.post("/api/library/do", query: ["what": what, "a": String(a), "b": String(b)])
            await load()
            return true
        } catch {
            fail(error)
            return false
        }
    }

    func fail(_ error: Error) {
        errorText = (error as? BrewAPIError)?.message ?? error.localizedDescription
    }

    func announce(_ text: String) {
        notice = text
        noticeTask?.cancel()
        noticeTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(4))
            if !Task.isCancelled { self?.notice = nil }
        }
    }
}
