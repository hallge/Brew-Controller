// What the Setup screens share: the controller's settings, the drinkers and the words for success and
// failure. Each sub-screen keeps its own form state and uses this to post.
import Foundation
import Observation

@MainActor
@Observable
final class SetupStore {
    var settings = SettingsModel()
    var drinkers = SetupDrinkers()
    var loaded = false
    /// The controller's firmware version and build date (GET /api/update); empty until read.
    var firmware = ""
    var built = ""
    /// How many add-ons the controller lists (GET /api/addons): the Add-ons row shows only when there are any.
    var addonCount = 0
    var error: String?
    var notice: String?

    @ObservationIgnored let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    func refresh() async {
        do {
            settings = try await api.settings()
            BrewTheme.shared.controllerThemeChanged(settings.uiTheme)
            loaded = true
        } catch let failure as BrewAPIError {
            error = failure.message
        } catch {
            self.error = String(describing: error)
        }
        if let value = try? await api.get("/api/coffee/ratings", as: SetupDrinkers.self) { drinkers = value }
        if let response = try? await api.getRaw("/api/update"),
           let object = try? JSONSerialization.jsonObject(with: response.body) as? [String: Any] {
            firmware = object["version"] as? String ?? ""
            built = object["built"] as? String ?? ""
        }
        if let list = try? await api.get("/api/addons", as: SetupAddonList.self) { addonCount = list.addons.count }
    }

    /// Runs a change; success shows `done`, failure the controller's words.
    @discardableResult
    func perform(_ done: String?, refresh again: Bool = true, _ work: () async throws -> Void) async -> Bool {
        error = nil
        notice = nil
        do {
            try await work()
            if let done { notice = done }
            if again { await refresh() }
            return true
        } catch let failure as BrewAPIError {
            error = failure.isConflict ? "Not while a shot is running." : failure.message
            return false
        } catch {
            self.error = String(describing: error)
            return false
        }
    }

    /// POST /api/settings with the fields sent only.
    func save(_ fields: [String: BrewJSON], done: String? = "Saved") async {
        await perform(done) {
            try await api.post("/api/settings", json: fields)
        }
    }

    // MARK: Appearance

    func chooseTheme(_ theme: BrewThemeID) async {
        await perform(nil, refresh: false) {
            try await BrewTheme.shared.choose(theme)
        }
        settings.uiTheme = BrewTheme.shared.controllerTheme.rawValue
    }

    // MARK: Drinkers

    func saveDrinkers(named: Bool, names: [String]) async {
        await perform("Saved") {
            try await api.post("/api/settings", json: ["namedDrinkers": .bool(named)])
            try await api.post("/api/drinkers", json: ["names": .array(names.map { BrewJSON.string($0) })])
        }
    }

    // MARK: Network settings the phone still uses

    /// "Allow Start from Home Assistant" (POST /api/net: it also needs the SSID the controller has).
    func saveWebStart(_ allowed: Bool) async {
        await perform("Saved") {
            try await api.post("/api/net", json: ["staSsid": .string(settings.net.staSsid), "webStartEnabled": .bool(allowed)])
        }
    }

    // MARK: Show in More

    /// The panels of the web page that More shows or hides (bit values from WebPage.h PANELS; the guide's bit means hidden).
    struct Panel: Identifiable, Sendable {
        var id: String
        var title: String
        var detail: String
        var bit: Int
        var inverted = false
    }

    static let panels: [Panel] = [
        Panel(id: "insights", title: "Insights", detail: "Your taste profile from your ratings, and charts: roast level against grind, coffee age against brew time, a brewing calendar, dial-in shots per bag.", bit: 1),
        Panel(id: "subscriptions", title: "Subscriptions", detail: "Coffee subscriptions: when the next delivery comes, skipping one, what is on its way.", bit: 4),
        Panel(id: "discover", title: "Discover", detail: "Coffees and subscriptions that match your taste, the shared coffee library and recipe book (browse, add, review), and the For you card on Home.", bit: 2),
        Panel(id: "community", title: "Community", detail: "What other owners share: where to start your grinder for what you are brewing, links, trending recipes, grinders, and what you share.", bit: 8),
        Panel(id: "logs", title: "Logs", detail: "The controller's log, live, for when something needs looking into.", bit: 16),
        Panel(id: "start", title: "Getting started", detail: "A step-by-step guide for a new controller: scale, bots, machine, grinder, recipe, coffee and the first shot.", bit: 64, inverted: true),
    ]

    func panelShown(_ panel: Panel) -> Bool {
        let set = settings.webPanels & panel.bit != 0
        return panel.inverted ? !set : set
    }

    func setPanel(_ panel: Panel, shown: Bool) async {
        let turnOn = panel.inverted ? !shown : shown
        let mask = turnOn ? (settings.webPanels | panel.bit) : (settings.webPanels & ~panel.bit)
        await save(["webPanels": .int(mask)], done: shown ? "Shown" : "Hidden")
    }
}
