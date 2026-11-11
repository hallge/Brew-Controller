// GET /api/status (Web.cpp handleStatus). Field names are the controller's own.
import Foundation

struct StatusModel: Codable, Sendable, Equatable {
    var apiVersion: Int = 1
    var maintenanceDue: Int = 0
    var capabilities: [String] = []
    var state: String = "BOOT"
    var weightG: Double = 0
    var flowGS: Double = 0
    var shotTimeS: Double = 0
    var targetWeightG: Double = 0
    var stopOffsetG: Double = 0
    var machineStyle: String = ""
    var targetCue: Bool = false
    var awaitingDrip: Bool = false
    var recipe: String = ""
    var scaleConnected: Bool = false
    var scaleResting: Bool = false
    var scaleStartPending: Bool = false
    var botConnected: Bool = false
    var botPaired: Bool = false
    var cupPresent: Bool = false
    var cupRequired: Bool = false
    /// Only while the scale is connected and reports it.
    var scaleBattery: Int?
    var weightFresh: Bool = false
    var samplePeriodMs: Int = 0
    var manualStopNeeded: Bool = false
    /// Only while blooming.
    var bloomLeftS: Double?
    var webStart: Bool = false
    /// Only when the bot's state is known.
    var botBattery: Int?
    var botSwitchMode: Bool?
    var lastShotId: Int = 0

    enum CodingKeys: String, CodingKey {
        case apiVersion = "api_version", maintenanceDue = "maintenance_due", capabilities, state
        case weightG = "weight_g", flowGS = "flow_g_s", shotTimeS = "shot_time_s"
        case targetWeightG = "target_weight_g", stopOffsetG = "stop_offset_g", machineStyle = "machine_style"
        case targetCue = "target_cue", awaitingDrip = "awaiting_drip", recipe
        case scaleConnected = "scale_connected", scaleResting = "scale_resting"
        case scaleStartPending = "scale_start_pending", botConnected = "bot_connected", botPaired = "bot_paired"
        case cupPresent = "cup_present", cupRequired = "cup_required", scaleBattery = "scale_battery"
        case weightFresh = "weight_fresh", samplePeriodMs = "sample_period_ms"
        case manualStopNeeded = "manual_stop_needed", bloomLeftS = "bloom_left_s", webStart = "web_start"
        case botBattery = "bot_battery", botSwitchMode = "bot_switch_mode", lastShotId = "last_shot_id"
    }
}

extension StatusModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        apiVersion = c.lenient(.apiVersion, 1)
        maintenanceDue = c.lenient(.maintenanceDue, 0)
        capabilities = c.lenient(.capabilities, [])
        state = c.lenient(.state, "BOOT")
        weightG = c.lenient(.weightG, 0)
        flowGS = c.lenient(.flowGS, 0)
        shotTimeS = c.lenient(.shotTimeS, 0)
        targetWeightG = c.lenient(.targetWeightG, 0)
        stopOffsetG = c.lenient(.stopOffsetG, 0)
        machineStyle = c.lenient(.machineStyle, "")
        targetCue = c.lenient(.targetCue, false)
        awaitingDrip = c.lenient(.awaitingDrip, false)
        recipe = c.lenient(.recipe, "")
        scaleConnected = c.lenient(.scaleConnected, false)
        scaleResting = c.lenient(.scaleResting, false)
        scaleStartPending = c.lenient(.scaleStartPending, false)
        botConnected = c.lenient(.botConnected, false)
        botPaired = c.lenient(.botPaired, false)
        cupPresent = c.lenient(.cupPresent, false)
        cupRequired = c.lenient(.cupRequired, false)
        scaleBattery = c.lenient(.scaleBattery)
        weightFresh = c.lenient(.weightFresh, false)
        samplePeriodMs = c.lenient(.samplePeriodMs, 0)
        manualStopNeeded = c.lenient(.manualStopNeeded, false)
        bloomLeftS = c.lenient(.bloomLeftS)
        webStart = c.lenient(.webStart, false)
        botBattery = c.lenient(.botBattery)
        botSwitchMode = c.lenient(.botSwitchMode)
        lastShotId = c.lenient(.lastShotId, 0)
    }
}
