// Models for the Setup screens that Core/Models does not have: drinkers, Home Assistant, the shared
// library link, diagnostics, the restore summary and the getting-started checklist. Lenient like
// Core/Models/LenientDecoding.swift.
import Foundation

// MARK: - Drinkers (GET /api/coffee/ratings: only the names are used)

struct SetupDrinkers: Codable, Sendable, Equatable {
    var named = false
    var drinkers: [String] = []
}

extension SetupDrinkers {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        named = c.lenient(.named, false)
        drinkers = c.lenient(.drinkers, [])
    }
}

// MARK: - Home Assistant (GET /api/homeassistant)

struct SetupHomeAssistant: Codable, Sendable, Equatable {
    var url: String = ""
    var rhEntity: String = ""
    var tempEntity: String = ""
    var tokenSet = false
    var webhookSet = false
    var mqttUrl: String = ""
    var mqttUser: String = ""
    var mqttPasswordSet = false
    var mqttConnected = false
    var problem: String = ""
    var mqttProblem: String = ""
    var lastWebhookCode: Int = 0

    enum CodingKeys: String, CodingKey {
        case url, problem
        case rhEntity = "rh_entity", tempEntity = "temp_entity", tokenSet = "token_set", webhookSet = "webhook_set"
        case mqttUrl = "mqtt_url", mqttUser = "mqtt_user", mqttPasswordSet = "mqtt_password_set"
        case mqttConnected = "mqtt_connected", mqttProblem = "mqtt_problem", lastWebhookCode = "last_webhook_code"
    }
}

extension SetupHomeAssistant {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        url = c.lenient(.url, "")
        rhEntity = c.lenient(.rhEntity, "")
        tempEntity = c.lenient(.tempEntity, "")
        tokenSet = c.lenient(.tokenSet, false)
        webhookSet = c.lenient(.webhookSet, false)
        mqttUrl = c.lenient(.mqttUrl, "")
        mqttUser = c.lenient(.mqttUser, "")
        mqttPasswordSet = c.lenient(.mqttPasswordSet, false)
        mqttConnected = c.lenient(.mqttConnected, false)
        problem = c.lenient(.problem, "")
        mqttProblem = c.lenient(.mqttProblem, "")
        lastWebhookCode = c.lenient(.lastWebhookCode, 0)
    }
}

/// GET /api/ambient/test: `{ok, problem, reading}`; `reading` is an object the app shows as text.
struct SetupAmbientTest: Codable, Sendable, Equatable {
    var ok = false
    var problem: String = ""
    var rhPct: Double?
    var tempC: Double?

    enum CodingKeys: String, CodingKey { case ok, problem, reading }
    enum ReadingKeys: String, CodingKey { case rhPct = "rh_pct", tempC = "temp_c" }
}

extension SetupAmbientTest {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        ok = c.lenient(.ok, false)
        problem = c.lenient(.problem, "")
        if let r = try? c.nestedContainer(keyedBy: ReadingKeys.self, forKey: .reading) {
            rhPct = r.lenient(.rhPct)
            tempC = r.lenient(.tempC)
        }
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(ok, forKey: .ok)
        try c.encode(problem, forKey: .problem)
    }
}

// MARK: - Shared library (GET /api/share)

struct SetupShareStatus: Codable, Sendable, Equatable {
    var keyReady = false
    var keyId: String = ""
    var algorithm: String = ""
    var realSignatures = false
    var canSend = false
    var repository: String = ""
    var viaRelay = false
    var relay: String = ""
    var configured = false
    var tokenSet = false
    var problem: String = ""
    var via: String = ""
    var toLibrary = false
    var waiting: Int = 0
    var sent: Int = 0
    var capacity: Int = 0

    enum CodingKeys: String, CodingKey {
        case algorithm, repository, relay, configured, problem, via, waiting, sent, capacity
        case keyReady = "key_ready", keyId = "key_id", realSignatures = "real_signatures", canSend = "can_send"
        case viaRelay = "via_relay", tokenSet = "token_set", toLibrary = "to_library"
    }
}

extension SetupShareStatus {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        keyReady = c.lenient(.keyReady, false)
        keyId = c.lenient(.keyId, "")
        algorithm = c.lenient(.algorithm, "")
        realSignatures = c.lenient(.realSignatures, false)
        canSend = c.lenient(.canSend, false)
        repository = c.lenient(.repository, "")
        viaRelay = c.lenient(.viaRelay, false)
        relay = c.lenient(.relay, "")
        configured = c.lenient(.configured, false)
        tokenSet = c.lenient(.tokenSet, false)
        problem = c.lenient(.problem, "")
        via = c.lenient(.via, "")
        toLibrary = c.lenient(.toLibrary, false)
        waiting = c.lenient(.waiting, 0)
        sent = c.lenient(.sent, 0)
        capacity = c.lenient(.capacity, 0)
    }
}

// MARK: - Diagnostics (GET /api/diagnostics/report)

struct SetupDiagReport: Codable, Sendable, Equatable {
    var on = false
    var relay: String = ""
    var relayDefault = true
    var waiting: Int = 0
    var since: Int = 0
    var lastSent: Int = 0
    var lastCode: Int = 0
    var lastProblem: String = ""
    var keyId: String = ""
    var bugSent: Int = 0
    var bugWaiting = false

    enum CodingKeys: String, CodingKey {
        case on, relay, waiting, since
        case relayDefault = "relay_default", lastSent = "last_sent", lastCode = "last_code"
        case lastProblem = "last_problem", keyId = "key_id", bugSent = "bug_sent", bugWaiting = "bug_waiting"
    }
}

extension SetupDiagReport {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        on = c.lenient(.on, false)
        relay = c.lenient(.relay, "")
        relayDefault = c.lenient(.relayDefault, true)
        waiting = c.lenient(.waiting, 0)
        since = c.lenient(.since, 0)
        lastSent = c.lenient(.lastSent, 0)
        lastCode = c.lenient(.lastCode, 0)
        lastProblem = c.lenient(.lastProblem, "")
        keyId = c.lenient(.keyId, "")
        bugSent = c.lenient(.bugSent, 0)
        bugWaiting = c.lenient(.bugWaiting, false)
    }
}

/// The kinds of problem report, in the board's words (value, label).
enum SetupBugKinds {
    static let all: [(String, String)] = [
        ("brewing", "Brewing: starting, stopping, weights"),
        ("grind_advice", "Grind advice or a starting point"),
        ("scale", "The scale"),
        ("pushers", "Button pushers (bots)"),
        ("machine_power", "Switching the machine on or off"),
        ("web_page", "The app's screens"),
        ("sharing", "Sharing and the community library"),
        ("firmware_update", "Updates"),
        ("other", "Something else"),
    ]
}

// MARK: - Restore (POST /api/restore)

struct SetupRestoreSummary: Codable, Sendable, Equatable {
    var created: Int = 0
    var firmware: String = ""
    var keyId: String = ""
    var history = false
    var secrets = false
    var records: Int = 0
    var files: Int = 0
    var bytes: Int = 0

    enum CodingKeys: String, CodingKey {
        case created, firmware, history, secrets, records, files, bytes
        case keyId = "key_id"
    }
}

extension SetupRestoreSummary {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        created = c.lenient(.created, 0)
        firmware = c.lenient(.firmware, "")
        keyId = c.lenient(.keyId, "")
        history = c.lenient(.history, false)
        secrets = c.lenient(.secrets, false)
        records = c.lenient(.records, 0)
        files = c.lenient(.files, 0)
        bytes = c.lenient(.bytes, 0)
    }
}

// MARK: - Getting started (GET /api/start)

struct SetupStart: Codable, Sendable, Equatable {
    var scalePaired = false
    var scaleConnected = false
    var botPaired = false
    var botConnected = false
    var powerBotPaired = false
    var machineMaker: String = ""
    var machineModel: String = ""
    var grinderModel: String = ""
    var grinderSet = false
    var grinderRange = false
    var recipes: Int = 0
    var coffeesOnRecipe: Int = 0
    var shots = false

    enum CodingKeys: String, CodingKey {
        case scale, machine, grinder, recipes, shots
        case botPair = "brew_bot", powerBot = "power_bot", coffeesOnRecipe = "coffees_on_recipe"
    }
    enum PairKeys: String, CodingKey { case paired, connected }
    enum MachineKeys: String, CodingKey { case maker, model }
    enum GrinderKeys: String, CodingKey { case model, set, range }
}

extension SetupStart {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        if let s = try? c.nestedContainer(keyedBy: PairKeys.self, forKey: .scale) {
            scalePaired = s.lenient(.paired, false)
            scaleConnected = s.lenient(.connected, false)
        }
        if let b = try? c.nestedContainer(keyedBy: PairKeys.self, forKey: .botPair) {
            botPaired = b.lenient(.paired, false)
            botConnected = b.lenient(.connected, false)
        }
        if let p = try? c.nestedContainer(keyedBy: PairKeys.self, forKey: .powerBot) {
            powerBotPaired = p.lenient(.paired, false)
        }
        if let m = try? c.nestedContainer(keyedBy: MachineKeys.self, forKey: .machine) {
            machineMaker = m.lenient(.maker, "")
            machineModel = m.lenient(.model, "")
        }
        if let g = try? c.nestedContainer(keyedBy: GrinderKeys.self, forKey: .grinder) {
            grinderModel = g.lenient(.model, "")
            grinderSet = g.lenient(.set, false)
            grinderRange = g.lenient(.range, false)
        }
        recipes = c.lenient(.recipes, 0)
        coffeesOnRecipe = c.lenient(.coffeesOnRecipe, 0)
        shots = c.lenient(.shots, false)
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(recipes, forKey: .recipes)
        try c.encode(shots, forKey: .shots)
    }
}
