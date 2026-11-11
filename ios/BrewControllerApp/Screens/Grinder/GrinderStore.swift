// What the Grinder screen shows and does: reads the routes, posts the changes, keeps the words for errors.
import Foundation
import Observation

@MainActor
@Observable
final class GrinderStore {
    var library = LibraryModel()
    var advice = GrindAdviceModel()
    var settings = GrinderAdviceSettings()
    var maintenance = MaintenanceModel()
    var shots: [ShotModel] = []
    /// nil: the shots could not be read (a shot is running).
    var shotsAvailable = false
    var sharedGrinders: [GrinderSharedEntry] = []
    var loaded = false
    var error: String?
    var notice: String?

    private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    var grinder: GrinderModel { library.grinder }
    var dial: BrewFormat.Dial { grinder.dial }

    // MARK: Reading

    func refresh() async {
        async let libraryResult = capture { try await self.api.library() }
        async let adviceResult = capture { try await self.api.grindAdvice() }
        async let settingsResult = capture { try await self.api.get("/api/settings", as: GrinderAdviceSettings.self) }
        async let maintenanceResult = capture { try await self.api.maintenance() }
        async let sharedResult = capture { try await self.api.get("/api/community/grinders", as: GrinderSharedList.self) }
        let (l, a, s, m, g) = await (libraryResult, adviceResult, settingsResult, maintenanceResult, sharedResult)
        if let l { library = l }
        if let a { advice = a }
        if let s { settings = s }
        if let m { maintenance = m }
        if let g { sharedGrinders = g.grinders }
        loaded = true
        await refreshShots()
    }

    func refreshShots() async {
        do {
            shots = try await api.shots(limit: 500)
            shotsAvailable = true
        } catch {
            shotsAvailable = false
        }
    }

    private func capture<T: Sendable>(_ work: @Sendable () async throws -> T) async -> T? {
        do {
            return try await work()
        } catch let failure as BrewAPIError {
            error = failure.message
            return nil
        } catch {
            self.error = String(describing: error)
            return nil
        }
    }

    // MARK: Doing

    /// Runs a change; on success refreshes and shows `done`, on failure shows the controller's words.
    @discardableResult
    func perform(_ done: String?, _ work: () async throws -> Void) async -> Bool {
        error = nil
        notice = nil
        do {
            try await work()
            if let done { notice = done }
            await refresh()
            return true
        } catch let failure as BrewAPIError {
            error = failure.isConflict && failure.message.isEmpty ? "Not while a shot is running." : failure.message
            return false
        } catch {
            self.error = String(describing: error)
            return false
        }
    }

    /// "I moved the grinder to": POST /api/grind/set?um=
    func setGrind(_ um: Int) async -> Bool {
        await perform("Grind setting saved") {
            try await api.post("/api/grind/set", query: ["um": String(um)])
        }
    }

    /// One of the advice settings, posted through /api/settings (only the field sent changes).
    func saveSetting(_ key: String, _ value: BrewJSON) async {
        await perform(nil) {
            try await api.post("/api/settings", json: [key: value])
        }
    }

    /// Upkeep: "done" today, or remind after `every` shots.
    func upkeepDone(_ job: String) async {
        await perform("Done today") {
            try await api.post("/api/maintenance", query: ["job": job, "action": "done"])
        }
    }

    func upkeepEvery(_ job: String, _ every: Int) async {
        await perform(nil) {
            try await api.post("/api/maintenance", query: ["job": job, "action": "every", "every": String(every)])
        }
    }

    func job(_ id: String) -> MaintenanceJob? { maintenance.jobs.first { $0.id == id } }

    // MARK: Second grinder, sharing

    func switchGrinder() async {
        await perform("Now using the other grinder") {
            try await api.post("/api/library/do", query: ["what": "grinder_switch", "a": "0", "b": "0"])
        }
    }

    func removeOtherGrinder() async {
        await perform("Second grinder removed") {
            try await api.post("/api/library/do", query: ["what": "grinder_remove_other", "a": "0", "b": "0"])
        }
    }

    func share(grinderId: Int) async {
        await perform("Queued for the shared library") {
            try await api.post("/api/share/grinder", query: ["id": String(grinderId)])
        }
    }

    /// POST /api/grinder with the form's body.
    func saveGrinder(_ body: [String: BrewJSON], done: String) async -> Bool {
        await perform(done) {
            try await api.post("/api/grinder", json: body)
        }
    }
}
