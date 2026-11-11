// BrewModel: what the Brew tab reads besides the 10 Hz live status and /api/screen (the library, the
// grind advice, the machine power, the most recent shot, the extras of the web Home), and every
// action the tab sends. Actions go through RetryingTask so the start-up 409 ("not while a shot is
// running" with no shot in hand) is tried again quietly. Stop, Tare, Start and Done never come
// through here: they are `LiveModel.send` (bbw_command), never queued behind a request.
import Foundation
import Observation

// MARK: - Model

@MainActor
@Observable
final class BrewModel {
    static let shared = BrewModel()

    private(set) var library: LibraryModel?
    private(set) var grind: GrindAdviceModel?
    private(set) var power: PowerModel?
    private(set) var settings: SettingsModel?
    private(set) var status: StatusModel?
    /// The most recent shot with its curve.
    private(set) var lastShot: ShotDataModel?
    private(set) var previous: BrewPreviousAdvice?
    private(set) var community: BrewCommunityInfo?
    private(set) var forYou: BrewTasteRecommendation?
    /// A message for an alert (an action the controller refused).
    var errorMessage: String?

    @ObservationIgnored private let api: BrewAPI
    @ObservationIgnored private var powerTask: Task<Void, Never>?

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Derived

    /// The grinder's dial, for settings written the way the grinder reads.
    var dial: BrewFormat.Dial { library?.grinder.dial ?? .microns }

    /// The recipe entry in use (its preset, recipe definition and coffee).
    var activePreset: PresetModel? { library?.activePresetEntry }
    var activeRecipeDef: RecipeDefModel? { activePreset.flatMap { library?.recipe(id: $0.recipeId) } }
    var activeCoffee: CoffeeModel? {
        guard let preset = activePreset, preset.coffeeId != 0 else { return nil }
        return library?.coffee(id: preset.coffeeId)
    }

    /// The SwitchBot is in Switch mode (it must be in Press mode).
    var botSwitchMode: Bool { status?.botSwitchMode ?? false }

    /// "Discover" is on in the web page's panels (bit 2).
    var discoverOn: Bool { (settings?.webPanels ?? 0) & 2 != 0 }

    // MARK: Loading

    /// Everything the tab shows besides the live status. Each part fails quietly: the tab keeps the last value.
    func refreshAll() async {
        // Not while a shot is in hand: the library routes answer 409 then, and the controller has enough to do.
        guard !LiveModel.shared.live.isShotInHand else { return }
        let api = self.api
        async let lib = try? api.library()
        async let advice = try? api.grindAdvice()
        async let pw = try? api.power()
        async let set = try? api.settings()
        async let stat = try? api.status()
        let (l, a, p, s, st) = await (lib, advice, pw, set, stat)
        if let st { status = st }
        if let l { library = l }
        if let a { grind = a }
        if let p { power = p }
        if let s { settings = s }
        await refreshLastShot()
        await refreshExtras()
    }

    /// The most recent shot and the advice recorded after it.
    func refreshLastShot() async {
        let live = LiveModel.shared.live
        guard !live.isShotInHand, live.lastShotID != 0 else { return }
        if let data = try? await api.shotData(id: Int(live.lastShotID)) { lastShot = data }
        if let list: [BrewPreviousAdvice] = try? await api.get("/api/contexts", query: ["limit": "1"]) {
            previous = list.first
        }
    }

    /// Community card and For you.
    func refreshExtras() async {
        if let preset = activePreset, preset.coffeeId != 0, preset.recipeId != 0 {
            community = try? await api.get("/api/community",
                                           query: ["coffee": String(preset.coffeeId), "recipe": String(preset.recipeId)],
                                           as: BrewCommunityInfo.self)
        } else {
            community = nil
        }
        if discoverOn {
            forYou = try? await api.get("/api/taste/recommend", as: BrewTasteRecommendation.self)
        } else {
            forYou = nil
        }
    }

    // MARK: Actions

    /// Runs an action that changes the controller, trying again after the start-up 409, then reloads.
    @discardableResult
    func perform(_ operation: @escaping @Sendable () async throws -> Void) async -> Bool {
        let task = RetryingTask<Bool>(maxAttempts: 6, firstDelay: .milliseconds(300), retryConflicts: true) {
            try await operation()
            return true
        }
        await task.run()
        LiveModel.shared.requestScreenRefresh()
        if let error = task.phase.error {
            errorMessage = error.message
            return false
        }
        await refreshAll()
        return true
    }

    func selectRecipe(_ index: Int) async {
        let api = self.api
        await perform { _ = try await api.screenAction("select_recipe", ["index": String(index)]) }
    }

    func nextRecipe() async {
        let api = self.api
        await perform { _ = try await api.screenAction("next_recipe") }
    }

    /// The edit screen's SAVE.
    func saveEdit(_ draft: BrewRecipeDraft, index: Int) async -> Bool {
        let api = self.api
        let args = draft.arguments(index: index)
        return await perform { _ = try await api.screenAction("apply_preset_edit", args) }
    }

    /// BAG FINISHED (the second tap).
    func finishBag() async -> Bool {
        let api = self.api
        return await perform { _ = try await api.screenAction("finish_active_coffee") }
    }

    /// SET / KEEP / NOT NOW on a grind question. Returns the setting the editor should open on (the
    /// no-grind question's SET), if any.
    func answerGrind(use: Bool) async -> Int? {
        let api = self.api
        var editUm: Int?
        let box = BrewResultBox()
        let ok = await perform {
            let response = try await api.screenAction("answer_grind_reco", ["use": use ? "true" : "false"])
            if let object = try? JSONSerialization.jsonObject(with: response.body) as? [String: Any],
               let um = object["edit_grind_um"] as? Int {
                box.value = um
            }
        }
        if ok { editUm = box.value }
        return editUm
    }

    /// IGNORE SHOT / COUNT FOR GRIND.
    func vote(shotId: Int, ignore: Bool) async {
        let api = self.api
        await perform { _ = try await api.screenAction("vote", ["shot_id": String(shotId), "vote": ignore ? "ignore" : "count"]) }
    }

    /// PRESS BUTTON on a fault: one manual press of the brew bot.
    func manualPress() {
        LiveModel.shared.send(.press)
    }

    // MARK: Machine power

    /// The power button: presses the machine's button through the power bot, then watches the answer.
    func pressPower() async {
        let api = self.api
        let ok = await perform { _ = try await api.post("/api/power", json: ["action": "press"]) }
        if ok { watchPower() }
    }

    /// The state pill: says whether the machine is on or off, pressing nothing.
    func believePower(on: Bool) async {
        let api = self.api
        await perform { _ = try await api.post("/api/power", json: ["action": on ? "believe_on" : "believe_off"]) }
    }

    /// Polls /api/power for up to about 3.5 minutes after a press (as the web page does).
    private func watchPower() {
        powerTask?.cancel()
        powerTask = Task { [weak self] in
            for _ in 0..<70 {
                try? await Task.sleep(for: .seconds(3))
                guard !Task.isCancelled, let self else { return }
                if let p = try? await self.api.power() {
                    self.power = p
                    if !p.working { return }
                }
            }
        }
    }

    // MARK: Most recent shot

    /// Stars (0 clears) and taste (0 none, 1 sour, 2 balanced, 3 bitter).
    func rate(shotId: Int, rating: Int, taste: Int) async {
        let api = self.api
        await perform {
            _ = try await api.post("/api/shot/rate", query: ["id": String(shotId), "rating": String(rating), "taste": String(taste)])
        }
    }

    /// Percent TDS from a refractometer; 0 clears it.
    func setTDS(shotId: Int, tds: Double) async {
        let api = self.api
        await perform {
            _ = try await api.post("/api/shot/tds", query: ["id": String(shotId), "tds": BrewFormat.decimal(tds, 2)])
        }
    }
}

/// Carries a value out of a @Sendable closure.
private final class BrewResultBox: @unchecked Sendable {
    var value: Int?
}
