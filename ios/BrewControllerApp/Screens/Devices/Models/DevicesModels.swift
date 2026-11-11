// Models for the Devices screen that Core/Models does not have: the parts of GET /api/devices that Core's
// DevicesModel leaves out (problems, power and stop bot details, memory), and the pusher keys.
import Foundation

struct DevicesDetail: Codable, Sendable, Equatable {
    var botProblem: String = ""
    var powerProblem: String = ""
    var stopProblem: String = ""
    var powerBattery: Int?
    var powerFirmware: String = ""
    var stopBattery: Int?

    enum CodingKeys: String, CodingKey {
        case botProblem = "bot_problem", powerProblem = "power_problem", stopProblem = "stop_problem"
        case powerBattery = "power_battery", powerFirmware = "power_firmware", stopBattery = "stop_battery"
    }
}

extension DevicesDetail {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        botProblem = c.lenient(.botProblem, "")
        powerProblem = c.lenient(.powerProblem, "")
        stopProblem = c.lenient(.stopProblem, "")
        powerBattery = c.lenient(.powerBattery)
        powerFirmware = c.lenient(.powerFirmware, "")
        stopBattery = c.lenient(.stopBattery)
    }
}

/// GET /api/pusher?role=: which keys are stored (the keys themselves are write-only).
struct DevicesPusher: Codable, Sendable, Equatable {
    var role: String = "brew"
    var tuyaUuid: String = ""
    var tuyaDeviceId: String = ""
    var tuyaLocalKeySet = false
    /// 0 Fingerbot, 1 CubeTouch.
    var tuyaVariant: Int = 0
    var microbotTokenSet = false
    /// Power and stop bots only.
    var passwordSet = false

    enum CodingKeys: String, CodingKey {
        case role
        case tuyaUuid = "tuya_uuid", tuyaDeviceId = "tuya_device_id", tuyaLocalKeySet = "tuya_local_key_set"
        case tuyaVariant = "tuya_variant", microbotTokenSet = "microbot_token_set", passwordSet = "password_set"
    }
}

extension DevicesPusher {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        role = c.lenient(.role, "brew")
        tuyaUuid = c.lenient(.tuyaUuid, "")
        tuyaDeviceId = c.lenient(.tuyaDeviceId, "")
        tuyaLocalKeySet = c.lenient(.tuyaLocalKeySet, false)
        tuyaVariant = c.lenient(.tuyaVariant, 0)
        microbotTokenSet = c.lenient(.microbotTokenSet, false)
        passwordSet = c.lenient(.passwordSet, false)
    }
}

/// GET /api/bot/test: `brew` is "none", "waiting", "pressed" or "failed".
struct DevicesBotTest: Codable, Sendable, Equatable {
    var ok = true
    var brew: String = "none"
}

extension DevicesBotTest {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        ok = c.lenient(.ok, true)
        brew = c.lenient(.brew, "none")
    }
}
