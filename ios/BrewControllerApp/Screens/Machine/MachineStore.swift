// What the Machine screen shows and does.
import Foundation
import Observation

@MainActor
@Observable
final class MachineStore {
    var power = PowerModel()
    var settings = SettingsModel()
    var maintenance = MaintenanceModel()
    var machine = MachineModel()
    var shared: [MachineSharedEntry] = []
    var reviewNote = ""
    var events: [MachinePowerEvent] = []
    var loaded = false
    /// The last GET /api/power worked (the schedule on screen is the controller's, not the empty default).
    var powerLoaded = false
    var error: String?
    var notice: String?

    private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    func refresh() async {
        async let p = capture { try await self.api.power() }
        async let s = capture { try await self.api.settings() }
        async let m = capture { try await self.api.maintenance() }
        async let mc = capture { try await self.api.machine() }
        async let sh = capture { try await self.api.get("/api/community/machines", as: MachineSharedList.self) }
        async let rn = capture { try await self.api.get("/api/machine", as: MachineReviewNote.self) }
        async let ev = capture { try await self.api.get("/api/power/events", query: ["days": "30"], as: [MachinePowerEvent].self) }
        let (power, settings, maintenance, machine, shared, review, events) = await (p, s, m, mc, sh, rn, ev)
        if let power {
            self.power = power
            powerLoaded = true
        }
        if let settings { self.settings = settings }
        if let maintenance { self.maintenance = maintenance }
        if let machine { self.machine = machine }
        if let shared { self.shared = shared.machines }
        if let review { reviewNote = review.note }
        if let events { self.events = events }
        loaded = true
    }

    /// Nothing else on the phone calls POST /api/time, so a visit gives the controller the phone's clock (it
    /// keeps one that is already set) and its time zone as a POSIX rule: the schedule follows daylight saving
    /// with it. Only when the controller's rule differs from the phone's or its clock is not set.
    func syncClock() async {
        guard powerLoaded else { return }
        let rule = MachineTimeZone.posix()
        if power.tzRule == rule && power.clockSet { return }
        do {
            try await api.post("/api/time", query: ["epoch": String(Int(Date().timeIntervalSince1970)), "tz": rule])
            if let fresh = try? await api.power() { power = fresh }
        } catch {
            // The schedule note says what the controller has; nothing more to show here.
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
            error = failure.message
            return false
        } catch {
            self.error = String(describing: error)
            return false
        }
    }

    // MARK: Power

    /// "on", "off", "press", "believe_on" or "believe_off".
    func powerAction(_ action: String) async {
        let said = action.hasPrefix("believe") ? "Noted" : "Power button: on its way"
        await perform(said) {
            try await api.post("/api/power", json: ["action": .string(action)])
        }
        // The press takes a few seconds; look again.
        try? await Task.sleep(for: .seconds(1))
        if let fresh = try? await api.power() { power = fresh }
    }

    func saveSchedule(_ entries: [PowerScheduleEntry]) async {
        let zone = TimeZone.current
        let offsetMin = zone.secondsFromGMT(for: Date()) / 60
        let schedule: [BrewJSON] = entries.map {
            .object(["enabled": .bool($0.enabled), "on": .bool($0.on), "minute": .int($0.minute), "days": .int($0.days)])
        }
        await perform("Schedule saved") {
            try await api.post("/api/power", json: [
                "action": "schedule", "schedule": .array(schedule), "utc_offset_min": .int(offsetMin),
                "tz_rule": .string(power.tzRule),
            ])
        }
    }

    // MARK: Options

    func saveOption(_ key: String, _ value: BrewJSON) async {
        await perform("Saved") {
            try await api.post("/api/settings", json: [key: value])
        }
    }

    // MARK: Upkeep

    func job(_ id: String) -> MaintenanceJob? { maintenance.jobs.first { $0.id == id } }

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

    // MARK: Make and model

    func saveMachine(_ body: [String: BrewJSON]) async {
        await perform("Machine saved") {
            try await api.post("/api/machine", json: body)
        }
    }

    func shareMachine() async {
        await perform("Queued for the shared library") {
            try await api.post("/api/share/machine")
        }
    }
}
