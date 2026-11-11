// The Recipes screens' state: the library (recipes, presets, grinder) and where each recipe stands with the
// recipe book. Every change goes through the board's own routes (src/net/Web.cpp), then the library is
// read again so the screen shows what the controller now has.
import Foundation
import Observation

/// A recipe as the form edits it. `RecipeDefModel` is what the controller sends; this is what goes back.
struct RecipeDraft: Equatable, Sendable {
    var name: String = ""
    var doseG: Double = 18
    var useRatio: Bool = false
    var targetWeightG: Double = 36
    var ratio: Double = 2
    var targetTimeS: Double = 28
    /// 0 tap (the machine's own pre-infusion), 1 hold the button.
    var preinfusion: Int = 0
    var preinfusionHoldS: Double = 3
    var bloomPauseS: Double = 0
    /// 0 the machine's own time-out, 1 the bot after a set time.
    var timeLimit: Int = 0
    var maxShotTimeS: Double = 50
    var baselineOffsetG: Double = 1.5

    /// A new recipe: what the web page starts the form with.
    init() {}

    init(_ recipe: RecipeDefModel) {
        name = recipe.name
        doseG = recipe.doseG
        useRatio = recipe.useRatio
        targetWeightG = recipe.targetWeightG
        ratio = recipe.ratio
        targetTimeS = recipe.targetTimeS
        preinfusion = recipe.preinfusion
        preinfusionHoldS = recipe.preinfusionHoldS
        bloomPauseS = recipe.bloomPauseS
        timeLimit = recipe.timeLimit
        maxShotTimeS = recipe.maxShotTimeS
        baselineOffsetG = recipe.baselineOffsetG
    }

    /// In ratio mode the target out is dose times ratio (WebPage.h readRecipe).
    var effectiveTargetG: Double {
        useRatio ? (doseG * ratio * 10).rounded() / 10 : targetWeightG
    }

    /// A bloom pause forces a held pre-infusion (WebPage.h recipeForm).
    var effectivePreinfusion: Int { bloomPauseS > 0 ? 1 : preinfusion }

    /// The limits of the web form's fields (the controller clamps again).
    func clamped() -> RecipeDraft {
        var copy = self
        copy.name = String(name.trimmingCharacters(in: .whitespaces).prefix(23))
        copy.doseG = min(max(doseG, 5), 30)
        copy.targetWeightG = min(max(targetWeightG, 5), 200)
        copy.ratio = min(max(ratio, 1), 5)
        copy.targetTimeS = min(max(targetTimeS, 5), 60)
        copy.preinfusionHoldS = min(max(preinfusionHoldS, 0.3), 30)
        copy.bloomPauseS = min(max(bloomPauseS, 0), 45)
        copy.maxShotTimeS = min(max(maxShotTimeS, 5), 90)
        copy.baselineOffsetG = min(max(baselineOffsetG, 0), 10)
        return copy
    }

    /// The body of POST /api/recipe; id 0 is a new recipe.
    func body(id: Int) -> [String: BrewJSON] {
        let value = clamped()
        return [
            "id": .int(id),
            "name": .string(value.name),
            "targetWeightG": .double(value.effectiveTargetG),
            "useRatio": .bool(value.useRatio),
            "doseG": .double(value.doseG),
            "ratio": .double(value.ratio),
            "targetTimeS": .double(value.targetTimeS),
            "preinfusion": .int(value.effectivePreinfusion),
            "preinfusionHoldS": .double(value.preinfusionHoldS),
            "timeLimit": .int(value.timeLimit),
            "maxShotTimeS": .double(value.maxShotTimeS),
            "baselineOffsetG": .double(value.baselineOffsetG),
            "bloomPauseS": .double(value.bloomPauseS),
        ]
    }
}

@MainActor
@Observable
final class RecipeStore {
    private(set) var library: LibraryModel?
    private(set) var pending: RecipePendingModel?
    private(set) var isLoading = false
    /// A message for the alert: a refused change or an unreadable library.
    var errorText: String?
    /// A short confirmation ("Recipe saved"), cleared by itself.
    private(set) var notice: String?

    @ObservationIgnored private let api: BrewAPI
    @ObservationIgnored private var noticeTask: Task<Void, Never>?

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    var recipes: [RecipeDefModel] { library?.recipes ?? [] }

    /// How many controller entries (presets) use a recipe.
    func entryCount(_ recipeId: Int) -> Int {
        library?.presets.filter { $0.recipeId == recipeId }.count ?? 0
    }

    // MARK: Loading

    /// Reads the library (the controller may still be starting: a few tries) and the recipe book state.
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
        await loadPending()
    }

    /// The recipe book state is optional: without it the Share button just stays disabled.
    func loadPending() async {
        pending = try? await api.get("/api/library/pending", as: RecipePendingModel.self)
    }

    // MARK: Changes

    /// Saves a recipe (id 0: a new one). Returns the id, nil when the controller refused.
    @discardableResult
    func save(_ draft: RecipeDraft, id: Int) async -> Int? {
        let value = draft.clamped()
        guard !value.name.isEmpty else {
            errorText = "A recipe needs a name"
            return nil
        }
        do {
            let reply = try await api.post("/api/recipe", json: draft.body(id: id), as: RecipeIdReply.self)
            await load()
            show("Recipe saved")
            return reply.id
        } catch {
            fail(error)
            return nil
        }
    }

    /// A copy of the form's recipe, named "<name> 2", not on the controller yet.
    @discardableResult
    func duplicate(_ draft: RecipeDraft) async -> Int? {
        var copy = draft
        copy.name = String((draft.name + " 2").prefix(23))
        do {
            let reply = try await api.post("/api/recipe", json: copy.body(id: 0), as: RecipeIdReply.self)
            await load()
            show("Copy made: not on the controller yet")
            return reply.id
        } catch {
            fail(error)
            return nil
        }
    }

    func delete(recipe id: Int) async -> Bool {
        await act("recipe_delete", a: id, b: 0, message: nil)
    }

    /// Puts the recipe on the controller by itself (a controller entry with no coffee).
    func listAlone(recipe id: Int) async {
        _ = await act("preset_add", a: id, b: 0, message: nil)
    }

    /// Takes one controller entry off (`preset_delete`, a = the entry's index).
    func takeOff(entry index: Int) async {
        _ = await act("preset_delete", a: index, b: 0, message: nil)
    }

    /// Forgets what the recipe learned on this entry's coffee: the stop offset starts again.
    func resetOffset(entry index: Int) async {
        do {
            try await api.post("/api/offset/reset", query: ["recipe": String(index)])
            await load()
            show("Offset reset")
        } catch {
            fail(error)
        }
    }

    func share(recipe id: Int) async {
        do {
            try await api.post("/api/recipes/share", query: ["id": String(id)])
            show("Queued for the recipe book")
            try? await Task.sleep(for: .milliseconds(1500))
            await loadPending()
        } catch {
            fail(error)
        }
    }

    // MARK: Helpers

    private func act(_ what: String, a: Int, b: Int, message: String?) async -> Bool {
        do {
            try await api.post("/api/library/do", query: ["what": what, "a": String(a), "b": String(b)])
            await load()
            if let message { show(message) }
            return true
        } catch {
            fail(error)
            return false
        }
    }

    private func fail(_ error: Error) {
        errorText = (error as? BrewAPIError)?.message ?? error.localizedDescription
    }

    private func show(_ text: String) {
        notice = text
        noticeTask?.cancel()
        noticeTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(3))
            if !Task.isCancelled { self?.notice = nil }
        }
    }
}
