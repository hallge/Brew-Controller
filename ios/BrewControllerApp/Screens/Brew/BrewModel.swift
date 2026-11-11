// BrewModel: what the Brew tab reads besides the 10 Hz live status and /api/screen (the library, the
// grind advice, the machine power, the shots, the extras of the web Home), and every action the tab
// sends. Actions go through RetryingTask so the start-up 409 ("not while a shot is running" with no shot
// in hand) is tried again quietly. Stop, Tare, Start and Done never come through here: they are
// `LiveModel.send` (bbw_command), never queued behind a request.
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
    private(set) var devices: DevicesModel?
    private(set) var version = ""
    /// The newest shots, newest first (the last-shot card reads the first).
    private(set) var shots: [ShotModel] = []
    private(set) var shotsLoaded = false
    /// The curve of the newest shot, when it has one stored.
    private(set) var lastData: ShotDataModel?
    /// The shot you rated best of this coffee and recipe, drawn behind the newest one.
    private(set) var refShot: ShotModel?
    private(set) var refData: ShotDataModel?
    private(set) var previous: BrewPreviousAdvice?
    private(set) var community: BrewCommunityInfo?
    private(set) var forYou: BrewTasteRecommendation?
    private(set) var subscriptionReco: BrewSubscriptionRecommendation?
    private(set) var start: SetupStart?
    /// A message for an alert (an action the controller refused).
    var errorMessage: String?
    /// A message for an alert that is not an error.
    var noticeMessage: String?

    @ObservationIgnored private let api: BrewAPI
    @ObservationIgnored private var powerTask: Task<Void, Never>?
    @ObservationIgnored private var refreshing = false
    @ObservationIgnored private var refreshAgain = false

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

    /// The newest shot.
    var lastShot: ShotModel? { shots.first }

    /// A panel of the web page is on (More follows the same switches). Each switch has its own bit, but
    /// the guide's (64) means "hidden".
    func panelOn(_ bit: Int, inverted: Bool = false) -> Bool {
        // Until the settings are read nothing is hidden on a guess.
        guard let mask = settings?.webPanels else { return true }
        let set = mask & bit != 0
        return inverted ? !set : set
    }
    var insightsOn: Bool { panelOn(1) }
    var discoverOn: Bool { panelOn(2) }
    var subscriptionsOn: Bool { panelOn(4) }
    var communityOn: Bool { panelOn(8) }
    var logsOn: Bool { panelOn(16) }
    var startOn: Bool { panelOn(64, inverted: true) }

    /// The steps of Getting started on the phone (the board's Wi-Fi step has no use here).
    var startSteps: [Bool] {
        guard let x = start else { return [] }
        return [x.scalePaired, x.botPaired, !x.machineMaker.isEmpty, x.grinderSet && x.grinderRange,
                x.recipes > 0, x.coffeesOnRecipe > 0, x.shots]
    }

    // MARK: Loading

    /// Everything the tab shows besides the live status. Each part fails quietly: the tab keeps the last value.
    func refreshAll() async {
        // One load at a time; a request that comes in meanwhile makes it go round once more (it may have
        // been asked for because something changed after the load began).
        if refreshing {
            refreshAgain = true
            return
        }
        refreshing = true
        repeat {
            refreshAgain = false
            await loadAll()
        } while refreshAgain
        refreshing = false
    }

    private func loadAll() async {
        // Not while a shot is in hand: the library routes answer 409 then, and the controller has enough to do.
        guard !LiveModel.shared.live.isShotInHand else { return }
        let api = self.api
        async let lib = try? api.library()
        async let advice = try? api.grindAdvice()
        async let pw = try? api.power()
        async let set = try? api.settings()
        async let stat = try? api.status()
        async let dev = try? api.get("/api/devices", as: DevicesModel.self)
        async let begin = try? api.get("/api/start", as: SetupStart.self)
        let (l, a, p, s, st, d, b) = await (lib, advice, pw, set, stat, dev, begin)
        if let st { status = st }
        if let l { library = l }
        if let a { grind = a }
        if let p { power = p }
        if let s { settings = s }
        if let d { devices = d }
        if let b { start = b }
        await refreshLastShot()
        await refreshExtras()
    }

    /// The library alone, awaited: the edit sheet must not open on old values and save them back.
    /// Returns false when it could not be read (nothing is then opened).
    func refreshLibrary() async -> Bool {
        guard let fresh = try? await api.library() else { return false }
        library = fresh
        return true
    }

    /// The newest shots, the curve of the newest, the one it is compared with and the advice recorded after it.
    func refreshLastShot() async {
        guard !LiveModel.shared.live.isShotInHand else { return }
        guard let list = try? await api.shots(limit: 30) else { return }
        shots = list
        shotsLoaded = true
        guard let newest = list.first else {
            lastData = nil
            refShot = nil
            refData = nil
            previous = nil
            return
        }
        if newest.hasData {
            lastData = try? await api.shotData(id: newest.id)
        } else {
            lastData = nil
        }
        // The shot you rated best of this coffee and recipe (4 stars or more), drawn dashed behind it.
        let candidates = list.filter {
            $0.id != newest.id && $0.coffeeId == newest.coffeeId && $0.recipeId == newest.recipeId
                && $0.hasData && $0.rating >= 4
        }
        .sorted { ($0.rating, $0.id) > ($1.rating, $1.id) }
        if let best = candidates.first {
            if refShot?.id != best.id || refData == nil { refData = try? await api.shotData(id: best.id) }
            refShot = best
        } else {
            refShot = nil
            refData = nil
        }
        if let entries: [BrewPreviousAdvice] = try? await api.get("/api/contexts", query: ["limit": "1"]) {
            previous = entries.first
        }
    }

    /// Community card, For you and its chips.
    func refreshExtras() async {
        let api = self.api
        if let preset = activePreset, preset.coffeeId != 0 {
            // The recipe: the active entry's; the web page asks for the coffee's own entry.
            if let recipe = library?.recipe(id: preset.recipeId) {
                community = try? await api.get("/api/community",
                                               query: ["coffee": String(preset.coffeeId), "recipe": String(recipe.id)],
                                               as: BrewCommunityInfo.self)
            } else {
                community = nil
            }
        } else {
            community = nil
        }
        if discoverOn || subscriptionsOn {
            async let taste = try? api.get("/api/taste/recommend", query: ["drinker": "0"], as: BrewTasteRecommendation.self)
            async let subs = try? api.get("/api/subscriptions/recommend", query: ["drinker": "0"],
                                          as: BrewSubscriptionRecommendation.self)
            let (t, s) = await (taste, subs)
            forYou = discoverOn ? t : nil
            subscriptionReco = subscriptionsOn ? s : nil
        } else {
            forYou = nil
            subscriptionReco = nil
        }
        if version.isEmpty, let info = try? await api.get("/api/update", as: BrewVersionInfo.self) {
            version = info.version ?? ""
        }
    }

    /// The devices, the status and the settings: what the Settings sheet shows, refreshed while it is open.
    func refreshDevices() async {
        let api = self.api
        async let dev = try? api.get("/api/devices", as: DevicesModel.self)
        async let stat = try? api.status()
        async let set = try? api.settings()
        let (d, st, s) = await (dev, stat, set)
        if let d { devices = d }
        if let st { status = st }
        if let s { settings = s }
    }

    /// The machine's power state, on its own and often: polled every 5 s while Brew is on show.
    func refreshPower() async {
        if let p = try? await api.power() { power = p }
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

    /// IGNORE SHOT / COUNT FOR GRIND on the result panel.
    func vote(shotId: Int, ignore: Bool) async {
        let api = self.api
        await perform { _ = try await api.screenAction("vote", ["shot_id": String(shotId), "vote": ignore ? "ignore" : "count"]) }
    }

    /// The grind vote of a shot on the home card: 0 auto, 1 ignore, 2 count.
    func setGrindVote(shotId: Int, vote: Int) async {
        let api = self.api
        await perform { _ = try await api.post("/api/shot/vote", query: ["id": String(shotId), "vote": String(vote)]) }
    }

    /// What the owner says about the preparation: the dose weighed (0: not weighed), purged first, flow
    /// looked uneven. An uneven shot with no vote yet is set to Ignore, as the web page does.
    func setPrep(shot: ShotModel, dose: Double, purged: Bool, uneven: Bool) async {
        let api = self.api
        let id = shot.id
        let presets = shot.grindVote.isEmpty && uneven
        let ok = await perform {
            _ = try await api.post("/api/shot/prep", query: ["id": String(id), "dose": BrewFormat.decimal(dose, 1),
                                                              "purged": purged ? "1" : "0", "uneven": uneven ? "1" : "0"])
            if presets { _ = try await api.post("/api/shot/vote", query: ["id": String(id), "vote": "1"]) }
        }
        if ok {
            if presets {
                noticeMessage = "Saved. This uneven shot is set to Ignore for the grind advice; choose Count to use it anyway."
            } else if uneven {
                noticeMessage = "Saved. If this uneven shot should not steer the grind, choose Ignore."
            }
        }
    }

    /// "Use X as my starting grind": writes the setting into the active entry.
    func useStart(_ um: Int) async {
        guard let index = library?.activePreset else { return }
        let api = self.api
        await perform {
            _ = try await api.post("/api/library/do", query: ["what": "preset_grind", "a": String(index), "b": String(um)])
        }
    }

    /// PRESS BUTTON on a fault: one manual press of the brew bot.
    func manualPress() {
        LiveModel.shared.send(.press)
    }

    // MARK: Settings sheet (the board's gear panel)

    /// OFFSET LEARNING / OFFSET FIXED.
    func setLearning(_ on: Bool) async {
        let api = self.api
        await perform { _ = try await api.post("/api/settings", json: ["learningEnabled": .bool(on)]) }
    }

    /// PAIR NEAREST BOT; returns whether a bot was found.
    func pairNearestBot() async -> Bool {
        let api = self.api
        let box = BrewResultBox()
        await perform {
            let response = try await api.screenAction("pair_nearest_bot")
            if let object = try? JSONSerialization.jsonObject(with: response.body) as? [String: Any],
               let pairing = object["pairing"] as? Bool {
                box.value = pairing ? 1 : 0
            } else {
                box.value = 1
            }
        }
        try? await Task.sleep(for: .milliseconds(1500))
        await refreshAll()
        return (box.value ?? 0) == 1
    }

    /// FORGET SCALE / FORGET BOT: an empty address forgets.
    func forget(_ kind: String) async {
        let api = self.api
        await perform { _ = try await api.post("/api/pair", json: ["kind": .string(kind), "address": "", "type": 0]) }
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

    /// The curve of a shot for a Beanconqueror hand-off.
    func data(of shot: ShotModel) async -> ShotDataModel? {
        if lastData?.id == shot.id { return lastData }
        guard shot.hasData else { return nil }
        return try? await api.shotData(id: shot.id)
    }
}

/// Carries a value out of a @Sendable closure.
private final class BrewResultBox: @unchecked Sendable {
    var value: Int?
}
