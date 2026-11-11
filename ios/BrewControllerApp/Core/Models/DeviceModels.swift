// GET /api/devices, /api/power, /api/maintenance and /api/machine.
import Foundation

// MARK: - /api/devices

struct DevicesModel: Codable, Sendable, Equatable {
    var scaleConnected = false
    var scaleName: String = ""
    var scaleProtocol: String = ""
    var botConnected = false
    var botFirmware: String = ""
    var botBattery: Int?
    var botSwitchMode = false
    var botHoldS: Int = 0
    var botProtocol: String = ""
    var botLabel: String = ""
    var botCaps: [String] = []
    var powerConnected = false
    var powerProtocol: String = ""
    var powerLabel: String = ""
    var pairing: DevicePairing = DevicePairing()
    var stopConnected = false
    var stopLabel: String = ""
    var scaleCaps: [String] = []
    var scaleData: ScaleDataModel = ScaleDataModel()
    /// Devices seen in the last scan.
    var seen: [SeenDevice] = []

    enum CodingKeys: String, CodingKey {
        case pairing, seen
        case scaleConnected = "scale_connected", scaleName = "scale_name", scaleProtocol = "scale_protocol"
        case botConnected = "bot_connected", botFirmware = "bot_firmware", botBattery = "bot_battery"
        case botSwitchMode = "bot_switch_mode", botHoldS = "bot_hold_s", botProtocol = "bot_protocol"
        case botLabel = "bot_label", botCaps = "bot_caps", powerConnected = "power_connected"
        case powerProtocol = "power_protocol", powerLabel = "power_label", stopConnected = "stop_connected"
        case stopLabel = "stop_label", scaleCaps = "scale_caps", scaleData = "scale_data"
    }
}

extension DevicesModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        scaleConnected = c.lenient(.scaleConnected, false)
        scaleName = c.lenient(.scaleName, "")
        scaleProtocol = c.lenient(.scaleProtocol, "")
        botConnected = c.lenient(.botConnected, false)
        botFirmware = c.lenient(.botFirmware, "")
        botBattery = c.lenient(.botBattery)
        botSwitchMode = c.lenient(.botSwitchMode, false)
        botHoldS = c.lenient(.botHoldS, 0)
        botProtocol = c.lenient(.botProtocol, "")
        botLabel = c.lenient(.botLabel, "")
        botCaps = c.lenient(.botCaps, [])
        powerConnected = c.lenient(.powerConnected, false)
        powerProtocol = c.lenient(.powerProtocol, "")
        powerLabel = c.lenient(.powerLabel, "")
        pairing = c.lenient(.pairing, DevicePairing())
        stopConnected = c.lenient(.stopConnected, false)
        stopLabel = c.lenient(.stopLabel, "")
        scaleCaps = c.lenient(.scaleCaps, [])
        scaleData = c.lenient(.scaleData, ScaleDataModel())
        seen = c.lenient(.seen, [])
    }
}

/// The addresses paired for each role ("" when none).
struct DevicePairing: Codable, Sendable, Equatable {
    var power: String = ""
    var stop: String = ""
    var scale: String = ""
    var bot: String = ""
}

extension DevicePairing {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        power = c.lenient(.power, "")
        stop = c.lenient(.stop, "")
        scale = c.lenient(.scale, "")
        bot = c.lenient(.bot, "")
    }
}

struct ScaleDataModel: Codable, Sendable, Equatable {
    var frames: Int = 0
    var weightFrames: Int = 0
    var timerFrames: Int = 0
    var badFrames: Int = 0
    var splitFrames: Int = 0
    var dropped: Int = 0
    var battery: Int?
    var timerRunning = false
    var lastFrame: String = ""
    var zeroesItself = false

    enum CodingKeys: String, CodingKey {
        case frames, dropped, battery
        case weightFrames = "weight_frames", timerFrames = "timer_frames", badFrames = "bad_frames"
        case splitFrames = "split_frames", timerRunning = "timer_running", lastFrame = "last_frame"
        case zeroesItself = "zeroes_itself"
    }
}

extension ScaleDataModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        frames = c.lenient(.frames, 0)
        weightFrames = c.lenient(.weightFrames, 0)
        timerFrames = c.lenient(.timerFrames, 0)
        badFrames = c.lenient(.badFrames, 0)
        splitFrames = c.lenient(.splitFrames, 0)
        dropped = c.lenient(.dropped, 0)
        battery = c.lenient(.battery)
        timerRunning = c.lenient(.timerRunning, false)
        lastFrame = c.lenient(.lastFrame, "")
        zeroesItself = c.lenient(.zeroesItself, false)
    }
}

/// A device in range ("kind": "scale", "bot", "power", ...).
struct SeenDevice: Codable, Sendable, Equatable, Identifiable {
    var kind: String = ""
    var make: String?
    var name: String = ""
    var address: String = ""
    var type: Int = 0
    var rssi: Int = 0

    var id: String { address }
}

extension SeenDevice {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        kind = c.lenient(.kind, "")
        make = c.lenient(.make)
        name = c.lenient(.name, "")
        address = c.lenient(.address, "")
        type = c.lenient(.type, 0)
        rssi = c.lenient(.rssi, 0)
    }
}

// MARK: - /api/power

struct PowerModel: Codable, Sendable, Equatable {
    var paired = false
    /// "on", "off" or "unknown".
    var belief: String = "unknown"
    var working = false
    var last: String = ""
    var lastBySchedule = false
    var utcOffsetMin: Int = 0
    var tzRule: String = ""
    var utcOffsetMinNow: Int = 0
    var clockSet = false
    var schedule: [PowerScheduleEntry] = []

    enum CodingKeys: String, CodingKey {
        case paired, belief, working, last, schedule
        case lastBySchedule = "last_by_schedule", utcOffsetMin = "utc_offset_min", tzRule = "tz_rule"
        case utcOffsetMinNow = "utc_offset_min_now", clockSet = "clock_set"
    }
}

extension PowerModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        paired = c.lenient(.paired, false)
        belief = c.lenient(.belief, "unknown")
        working = c.lenient(.working, false)
        last = c.lenient(.last, "")
        lastBySchedule = c.lenient(.lastBySchedule, false)
        utcOffsetMin = c.lenient(.utcOffsetMin, 0)
        tzRule = c.lenient(.tzRule, "")
        utcOffsetMinNow = c.lenient(.utcOffsetMinNow, 0)
        clockSet = c.lenient(.clockSet, false)
        schedule = c.lenient(.schedule, [])
    }
}

struct PowerScheduleEntry: Codable, Sendable, Equatable {
    var enabled = false
    /// true: switch on; false: switch off.
    var on = true
    var minute: Int = 0
    /// Bit mask of weekdays.
    var days: Int = 127
}

extension PowerScheduleEntry {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        enabled = c.lenient(.enabled, false)
        on = c.lenient(.on, true)
        minute = c.lenient(.minute, 0)
        days = c.lenient(.days, 127)
    }
}

// MARK: - /api/maintenance

struct MaintenanceModel: Codable, Sendable, Equatable {
    var shotsTotal: Int = 0
    /// How many jobs are due.
    var due: Int = 0
    var jobs: [MaintenanceJob] = []

    enum CodingKeys: String, CodingKey {
        case due, jobs
        case shotsTotal = "shots_total"
    }
}

extension MaintenanceModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        shotsTotal = c.lenient(.shotsTotal, 0)
        due = c.lenient(.due, 0)
        jobs = c.lenient(.jobs, [])
    }
}

/// "backflush", "descale" or "grinder_clean".
struct MaintenanceJob: Codable, Sendable, Equatable, Identifiable {
    var id: String = ""
    var sinceShots: Int = 0
    /// 0: not scheduled.
    var everyShots: Int = 0
    var due = false
    var lastEpoch: Int = 0

    enum CodingKeys: String, CodingKey {
        case id, due
        case sinceShots = "since_shots", everyShots = "every_shots", lastEpoch = "last_epoch"
    }
}

extension MaintenanceJob {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, "")
        sinceShots = c.lenient(.sinceShots, 0)
        everyShots = c.lenient(.everyShots, 0)
        due = c.lenient(.due, false)
        lastEpoch = c.lenient(.lastEpoch, 0)
    }
}

// MARK: - /api/machine

struct MachineModel: Codable, Sendable, Equatable {
    var current: MachineProfile = MachineProfile()
    var builtins: [MachineProfile] = []
}

extension MachineModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        current = c.lenient(.current, MachineProfile())
        builtins = c.lenient(.builtins, [])
    }
}

/// An espresso machine profile: how the brew bot starts and stops it.
struct MachineProfile: Codable, Sendable, Equatable, Identifiable {
    var maker: String = ""
    var model: String = ""
    var control: String = ""
    /// "toggle_button", "start_button", "manual", "start_stop_buttons" or "hold_button".
    var style: String = ""
    var manualStart: String = ""
    var holdPreinfusion = false
    var tapPreinfusion = false
    var powerToggle = false
    var selfStopS: Int = 0
    var autoOffMin: Int = 0
    /// The built-in number (1...6); 0 when it is the owner's own.
    var builtin: Int = 0
    var libraryId: String = ""
    var canShare: Bool?
    var shareState: String?
    var changedSinceShared: Bool?

    var id: String { "\(builtin):\(maker):\(model)" }

    enum CodingKeys: String, CodingKey {
        case maker, model, control, style, builtin
        case manualStart = "manual_start", holdPreinfusion = "hold_preinfusion", tapPreinfusion = "tap_preinfusion"
        case powerToggle = "power_toggle", selfStopS = "self_stop_s", autoOffMin = "auto_off_min"
        case libraryId = "library_id", canShare = "can_share", shareState = "share_state"
        case changedSinceShared = "changed_since_shared"
    }
}

extension MachineProfile {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        maker = c.lenient(.maker, "")
        model = c.lenient(.model, "")
        control = c.lenient(.control, "")
        style = c.lenient(.style, "")
        manualStart = c.lenient(.manualStart, "")
        holdPreinfusion = c.lenient(.holdPreinfusion, false)
        tapPreinfusion = c.lenient(.tapPreinfusion, false)
        powerToggle = c.lenient(.powerToggle, false)
        selfStopS = c.lenient(.selfStopS, 0)
        autoOffMin = c.lenient(.autoOffMin, 0)
        builtin = c.lenient(.builtin, 0)
        libraryId = c.lenient(.libraryId, "")
        canShare = c.lenient(.canShare)
        shareState = c.lenient(.shareState)
        changedSinceShared = c.lenient(.changedSinceShared)
    }
}

// MARK: - Typed fetches

extension BrewAPI {
    func status() async throws -> StatusModel { try await get("/api/status") }
    func screen() async throws -> ScreenSnapshot { try await get("/api/screen") }
    func library() async throws -> LibraryModel { try await get("/api/library") }
    func settings() async throws -> SettingsModel { try await get("/api/settings") }
    func grindAdvice() async throws -> GrindAdviceModel { try await get("/api/grind") }
    func devices() async throws -> DevicesModel { try await get("/api/devices") }
    func power() async throws -> PowerModel { try await get("/api/power") }
    func maintenance() async throws -> MaintenanceModel { try await get("/api/maintenance") }
    func machine() async throws -> MachineModel { try await get("/api/machine") }
}
