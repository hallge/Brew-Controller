// What the Devices screen shows and does: the summary, the seen list with roles, pair, forget, swap,
// test press and the pusher keys.
import Foundation
import Observation

@MainActor
@Observable
final class DevicesStore {
    var devices = DevicesModel()
    var detail = DevicesDetail()
    var loaded = false
    var error: String?
    var notice: String?

    var pusherRole = "brew"
    var pusher = DevicesPusher()

    private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Reading

    func refresh() async {
        do {
            let response = try await api.getRaw("/api/devices")
            devices = try BrewAPI.decode(DevicesModel.self, from: response.body)
            detail = (try? BrewAPI.decode(DevicesDetail.self, from: response.body)) ?? DevicesDetail()
            loaded = true
        } catch let failure as BrewAPIError {
            error = failure.message
        } catch {
            self.error = String(describing: error)
        }
    }

    func loadPusher(_ role: String? = nil) async {
        if let role { pusherRole = role }
        if let value = try? await api.get("/api/pusher", query: ["role": pusherRole], as: DevicesPusher.self) {
            pusher = value
        }
    }

    // MARK: Helpers

    /// A paired device's signal as last heard advertising.
    func signal(for address: String) -> Int? {
        guard !address.isEmpty else { return nil }
        return devices.seen.first { $0.address.lowercased() == address.lowercased() }?.rssi
    }

    /// Does this seen device hold the role? `role`: scale, bot, power or stop.
    func holds(_ role: String, _ device: SeenDevice) -> Bool {
        let paired: String
        switch role {
        case "scale": paired = devices.pairing.scale
        case "bot": paired = devices.pairing.bot
        case "power": paired = devices.pairing.power
        default: paired = devices.pairing.stop
        }
        return !paired.isEmpty && paired.lowercased() == device.address.lowercased()
    }

    // MARK: Doing

    @discardableResult
    func perform(_ done: String?, _ work: () async throws -> Void) async -> Bool {
        error = nil
        notice = nil
        do {
            try await work()
            if let done { notice = done }
            return true
        } catch let failure as BrewAPIError {
            error = failure.message
            return false
        } catch {
            self.error = String(describing: error)
            return false
        }
    }

    /// POST /api/pair: an empty address forgets.
    func pair(_ kind: String, address: String, type: Int, done: String? = nil) async {
        await perform(done) {
            try await api.post("/api/pair", json: ["kind": .string(kind), "address": .string(address), "type": .int(type)])
        }
        try? await Task.sleep(for: .milliseconds(800))
        await refresh()
    }

    func forget(_ kind: String, label: String) async {
        await pair(kind, address: "", type: 0, done: "Forgot the \(label)")
    }

    /// The board's "pair the nearest bot" (the strongest signal).
    func pairNearestBot() async {
        var found = true
        let ok = await perform(nil) {
            let response = try await api.screenAction("pair_nearest_bot")
            if let object = try? JSONSerialization.jsonObject(with: response.body) as? [String: Any],
               let pairing = object["pairing"] as? Bool {
                found = pairing
            }
        }
        if ok { notice = found ? "Pairing the nearest bot" : "No bot found nearby" }
        try? await Task.sleep(for: .milliseconds(1500))
        await refresh()
    }

    /// Brew becomes power and power becomes brew (three pairings).
    func swapBots() async {
        let bot = devices.pairing.bot
        let power = devices.pairing.power
        guard !bot.isEmpty, !power.isEmpty else {
            error = "Both a brew bot and a power bot must be paired first"
            return
        }
        func type(_ address: String) -> Int { devices.seen.first { $0.address == address }?.type ?? 1 }
        let botType = type(bot)
        let powerType = type(power)
        await perform("Swapped") {
            try await api.post("/api/pair", json: ["kind": "bot", "address": "", "type": 0])
            try await api.post("/api/pair", json: ["kind": "power", "address": .string(bot), "type": .int(botType)])
            try await api.post("/api/pair", json: ["kind": "bot", "address": .string(power), "type": .int(powerType)])
        }
        try? await Task.sleep(for: .milliseconds(800))
        await refresh()
    }

    /// Test press: the brew test polls until the bot answers (up to 45 s); the power test only starts it.
    func testPress(role: String) async {
        let brew = role == "brew"
        let started = await perform(nil) {
            try await api.post("/api/bot/test", query: ["role": role])
        }
        guard started else { return }
        guard brew else {
            notice = "Power bot: pressing. Watch which one moves."
            try? await Task.sleep(for: .seconds(3))
            await refresh()
            return
        }
        notice = "Brew bot: waking it up..."
        for _ in 0..<45 {
            try? await Task.sleep(for: .seconds(1))
            let state = (try? await api.get("/api/bot/test", as: DevicesBotTest.self))?.brew ?? "none"
            if state == "pressed" {
                notice = "Brew bot pressed. That is the one that moved."
                await refresh()
                return
            }
            if state == "failed" { break }
        }
        notice = nil
        error = "The brew bot did not answer: is it in range and awake?"
        await refresh()
    }

    /// POST /api/pusher: empty key fields keep what is stored.
    func savePusher(uuid: String, deviceId: String, localKey: String, variant: Int, token: String, password: String) async {
        await perform("Keys saved") {
            try await api.post("/api/pusher", json: [
                "role": .string(pusherRole), "tuya_uuid": .string(uuid), "tuya_device_id": .string(deviceId),
                "tuya_local_key": .string(localKey), "tuya_variant": .int(variant),
                "microbot_token": .string(token), "password": .string(password),
            ])
        }
        await loadPusher()
    }

    /// The brew bot's SwitchBot password lives in the network settings (POST /api/net, which also takes
    /// the SSID and the Home Assistant start switch the controller already has).
    func saveBotPassword(_ password: String) async {
        await perform("Keys saved") {
            let current = try await api.settings()
            try await api.post("/api/net", json: [
                "staSsid": .string(current.net.staSsid), "botPassword": .string(password),
                "webStartEnabled": .bool(current.net.webStartEnabled),
            ])
        }
    }
}
