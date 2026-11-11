// What the Community screen shows and does: the feed and your link suggestions, trending recipes, what
// other owners settled on for what you are brewing, the shared grinder list and what you share.
import Observation
import SwiftUI

/// A line for the banner at the top of Community.
struct CommunityNotice: Equatable, Identifiable {
    var text: String
    var good: Bool
    var id = UUID()
}

/// A link suggestion as typed.
struct CommunitySuggestionDraft: Equatable {
    var url = ""
    var title = ""
    var kind = "video"
    var topics: [String] = []
    var why = ""
    var image = ""

    /// The body for POST /api/links/suggest.
    var json: [String: BrewJSON] {
        [
            "url": .string(url), "title": .string(title), "kind": .string(kind),
            "topics": .array(topics.map { BrewJSON.string($0) }), "why": .string(why), "image": .string(image),
        ]
    }
}

@MainActor
@Observable
final class CommunityStore {
    enum Phase: Equatable {
        case loading
        case loaded
    }

    private(set) var phase: Phase = .loading
    /// nil: the controller could not be asked.
    private(set) var links: CommunityLinks?
    /// The feed's topic filter ("" all).
    var topic = ""
    private(set) var trending: CommunityTrending?
    private(set) var grinders: [CommunityGrinder] = []
    private(set) var share: CommunityShareStatus?
    private(set) var settings: SettingsModel?
    private(set) var library = LibraryModel()
    private(set) var shots: [ShotModel] = []
    private(set) var now: CommunityNow?
    var notice: CommunityNotice?

    @ObservationIgnored private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Loading

    func load() async {
        async let linksResult = fetch(CommunityLinks.self, "/api/links")
        async let trendingResult = fetch(CommunityTrending.self, "/api/trending")
        async let grindersResult = fetch(CommunityGrinderList.self, "/api/community/grinders")
        async let shareResult = fetch(CommunityShareStatus.self, "/api/share")
        async let settingsResult = fetch(SettingsModel.self, "/api/settings")
        async let libraryResult = fetch(LibraryModel.self, "/api/library")
        async let shotsResult = fetch([ShotModel].self, "/api/shots", query: ["limit": "40"])
        links = await linksResult
        trending = await trendingResult
        grinders = ((await grindersResult)?.grinders ?? []).filter { !$0.model.isEmpty }
        share = await shareResult
        settings = await settingsResult
        if let library = await libraryResult { self.library = library }
        if let shots = await shotsResult { self.shots = shots }
        if let links, !links.listed.contains(where: { !$0.topics.isEmpty }) { topic = "" }
        await loadNow()
        phase = .loaded
    }

    /// What other owners settled on for the coffee and recipe in use.
    func loadNow() async {
        now = nil
        guard let preset = library.activePresetEntry, preset.coffeeId != 0, let coffee = library.coffee(id: preset.coffeeId),
              !coffee.libraryId.isEmpty else { return }
        now = await fetch(CommunityNow.self, "/api/community",
                          query: ["coffee": String(preset.coffeeId), "recipe": String(preset.recipeId)])
    }

    private func fetch<T: Decodable & Sendable>(_ type: T.Type, _ path: String,
                                                query: [String: String] = [:]) async -> T? {
        try? await api.get(path, query: query, as: type)
    }

    // MARK: Derived

    var sharingOn: Bool { settings?.shareEnabled ?? false }

    /// The coffee in use, and its recipe.
    var activeCoffee: CoffeeModel? {
        guard let preset = library.activePresetEntry else { return nil }
        return library.coffee(id: preset.coffeeId)
    }

    var activeRecipe: RecipeDefModel? {
        guard let preset = library.activePresetEntry else { return nil }
        return library.recipe(id: preset.recipeId)
    }

    /// The feed's links that have the chosen topic.
    var shownLinks: [CommunityLink] {
        let listed = links?.listed ?? []
        return topic.isEmpty ? listed : listed.filter { $0.topics.contains(topic) }
    }

    /// The topics at least one listed link has.
    var topicChoices: [(key: String, label: String)] {
        let listed = links?.listed ?? []
        return CommunityText.topics.filter { choice in listed.contains { $0.topics.contains(choice.key) } }
    }

    // MARK: Actions

    /// Reads a page for the suggestion form: POST the link, then poll up to 30 s.
    func readPage(_ url: String) async -> SubscriptionPreview? {
        guard url.lowercased().hasPrefix("http://") || url.lowercased().hasPrefix("https://") else {
            say("Paste a link starting https://", good: false)
            return nil
        }
        do {
            try await api.post("/api/subscriptions/preview", json: ["url": .string(url)])
        } catch {
            say(error)
            return nil
        }
        for _ in 0..<40 {
            try? await Task.sleep(for: .milliseconds(750))
            if Task.isCancelled { return nil }
            guard let preview = await fetch(SubscriptionPreview.self, "/api/subscriptions/preview") else { continue }
            if preview.url == url && preview.state != "waiting" && preview.state != "none" { return preview }
        }
        return nil
    }

    func suggest(_ draft: CommunitySuggestionDraft) async -> Bool {
        let url = draft.url.trimmingCharacters(in: .whitespaces)
        let title = draft.title.trimmingCharacters(in: .whitespaces)
        if url.isEmpty { say("Paste the link first", good: false); return false }
        if title.isEmpty { say("Give it a title", good: false); return false }
        if draft.topics.isEmpty { say("Pick at least one topic", good: false); return false }
        var checked = draft
        checked.url = url
        checked.title = title
        checked.why = draft.why.trimmingCharacters(in: .whitespacesAndNewlines)
        do {
            try await api.post("/api/links/suggest", json: checked.json)
            say("Sent for review: it shows in the feed once approved", good: true)
            links = await fetch(CommunityLinks.self, "/api/links") ?? links
            return true
        } catch {
            say(error)
            return false
        }
    }

    func delete(_ suggestion: CommunitySuggestion) async {
        do {
            try await api.post("/api/links/delete", query: ["id": String(suggestion.id)])
            say("Deleted", good: true)
            links = await fetch(CommunityLinks.self, "/api/links") ?? links
        } catch {
            say(error)
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

    /// Uses the community's start as the grinder setting of the preset in use (stored steps).
    func useStart(_ setting: Int) async {
        do {
            try await api.post("/api/library/do", query: ["what": "preset_grind", "a": String(library.activePreset),
                                                          "b": String(setting)])
            say("Starting grind set", good: true)
            if let library = await fetch(LibraryModel.self, "/api/library") { self.library = library }
        } catch {
            say(error)
        }
    }

    /// The curve of a shot, for the Reddit picture (nil when none is stored).
    func shotData(_ shot: ShotModel) async -> ShotDataModel? {
        guard shot.hasData else { return nil }
        return await fetch(ShotDataModel.self, "/api/shot/data", query: ["id": String(shot.id)])
    }

    private func say(_ text: String, good: Bool) {
        notice = CommunityNotice(text: text, good: good)
    }

    private func say(_ error: Error) {
        say((error as? BrewAPIError)?.message ?? String(describing: error), good: false)
    }
}
