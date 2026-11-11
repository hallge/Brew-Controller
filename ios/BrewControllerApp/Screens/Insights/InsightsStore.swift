// What the Insights screen shows: the shots, the library, the taste profile, the grind accuracy and the
// machine's power events, loaded from the in-process web API.
import Observation
import SwiftUI

@MainActor
@Observable
final class InsightsStore {
    enum Phase: Equatable {
        case loading
        case loaded
        /// The shots could not be read (a shot is running, or the controller is not ready).
        case failed(String)
    }

    private(set) var phase: Phase = .loading
    private(set) var shots: [ShotModel] = []
    private(set) var library = LibraryModel()
    private(set) var settings: SettingsModel?
    private(set) var ratings = InsightsRatings()
    private(set) var profile: InsightsTasteProfile?
    private(set) var accuracy: InsightsGrindAccuracy?
    private(set) var powerEvents: [InsightsPowerEvent] = []

    /// 0 is the household, 1...8 a named drinker.
    var drinker = 0
    /// nil: all coffees; else a coffee id (0: shots with no coffee).
    var coffeeFilter: Int?

    @ObservationIgnored private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Derived

    /// The shots of the chosen coffee (all of them without a filter).
    var filteredShots: [ShotModel] {
        guard let coffeeFilter else { return shots }
        return shots.filter { $0.coffeeId == coffeeFilter }
    }

    /// The coffees the shots were made with, for the picker.
    var coffeeChoices: [(id: Int, name: String)] {
        var seen = Set<Int>()
        var list: [(id: Int, name: String)] = []
        for shot in shots where !shot.coffee.isEmpty && !seen.contains(shot.coffeeId) {
            seen.insert(shot.coffeeId)
            list.append((id: shot.coffeeId, name: shot.coffee))
        }
        return list
    }

    var coffeeFilterName: String {
        guard let coffeeFilter else { return "All coffees" }
        return coffeeChoices.first { $0.id == coffeeFilter }?.name ?? "All coffees"
    }

    var tolerance: Double { InsightsMath.tolerance(settings) }

    var currentCoffeeId: Int { library.activePresetEntry?.coffeeId ?? 0 }

    func coffeeName(_ id: Int) -> String? {
        library.coffee(id: id)?.name
    }

    // MARK: Loading

    func load() async {
        if shots.isEmpty { phase = .loading }
        async let shotsResult = fetchShots()
        async let libraryResult = fetch(LibraryModel.self, "/api/library")
        async let settingsResult = fetch(SettingsModel.self, "/api/settings")
        async let ratingsResult = fetch(InsightsRatings.self, "/api/coffee/ratings")
        async let accuracyResult = fetch(InsightsGrindAccuracy.self, "/api/grind/accuracy")
        async let powerResult = fetch([InsightsPowerEvent].self, "/api/power/events", query: ["days": "90"])

        let loadedShots = await shotsResult
        if let library = await libraryResult { self.library = library }
        if let settings = await settingsResult { self.settings = settings }
        if let ratings = await ratingsResult { self.ratings = ratings }
        if let accuracy = await accuracyResult { self.accuracy = accuracy }
        if let events = await powerResult { powerEvents = events }
        switch loadedShots {
        case .success(let list):
            shots = list
            phase = .loaded
        case .failure(let error):
            // Keep what was there; say so only when there is nothing to show.
            if shots.isEmpty { phase = .failed(InsightsStore.words(for: error)) }
        }
        if !ratings.people.contains(where: { $0.id == drinker }) { drinker = 0 }
        await loadProfile()
    }

    /// The taste profile of the chosen drinker.
    func loadProfile() async {
        profile = await fetch(InsightsTasteProfile.self, "/api/taste/profile", query: ["drinker": String(drinker)])
    }

    func chooseDrinker(_ id: Int) async {
        drinker = id
        await loadProfile()
    }

    private func fetchShots() async -> Result<[ShotModel], BrewAPIError> {
        do {
            return .success(try await api.get("/api/shots", query: ["limit": "500"], as: [ShotModel].self))
        } catch let error as BrewAPIError {
            return .failure(error)
        } catch {
            return .failure(.invalidRequest(String(describing: error)))
        }
    }

    private func fetch<T: Decodable & Sendable>(_ type: T.Type, _ path: String,
                                                query: [String: String] = [:]) async -> T? {
        try? await api.get(path, query: query, as: type)
    }

    nonisolated static func words(for error: BrewAPIError) -> String {
        error.isConflict ? "The controller is brewing a shot. Insights are back when it is done." : error.message
    }
}
