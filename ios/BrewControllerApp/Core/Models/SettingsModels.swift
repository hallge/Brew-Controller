// GET /api/settings (Web.cpp handleGetSettings). camelCase, as the handler writes it. Post a change
// with `BrewAPI.post("/api/settings", json: ["uiTheme": .int(2)])`: only the fields sent change.
import Foundation

struct SettingsModel: Codable, Sendable, Equatable {
    var learningEnabled = true
    var learningFactor: Double = 0
    var adaptiveLearning = false
    var minValidWeightG: Double = 0
    var scaleTimeoutMs: Int = 0
    var dripObserveMs: Int = 0
    var minShotTimeMs: Int = 0
    var noFlowTimeoutS: Int = 0
    var cupMode: Int = 0
    var cupMinWeightG: Double = 0
    var autoStartAfterTare = false
    var scaleTimer = false
    var requireCup = false
    var keepPowerBotConnected = false
    var displayFlipped = false
    /// bbw::UiTheme number, 0...5.
    var uiTheme: Int = 0
    var powerOffWithScale = false
    var diagnosticsOn = false
    var namedDrinkers = false
    var webPanels: Int = 0
    var webPanelsChosen = false
    var scaleRestMin: Int = 0
    var actuationMode: Int = 0
    var pressDurationMs: Int = 0
    var releaseSettleMs: Int = 0
    var botHoldSeconds: Int = 0
    var grindUm: Int = 0
    var grindToleranceS: Double = 0
    var grindUmPerS: Double = 0
    var grindWindow: Int = 0
    var grindMaxStepUm: Int = 0
    /// 0 total time, 1 pour time.
    var grindTimeBasis: Int = 0
    var shareEnabled = false
    var shareCoffees = false
    var shareDialIns = false
    var shareShots = false
    var shareRatings = false
    var grindRatedTarget = false
    var grindConfidence = false
    var grindRoom = false
    var activeRecipe: Int = 0
    var maxRecipes: Int = 0
    var recipes: [SettingsRecipe] = []
    var net: SettingsNet = SettingsNet()

    var theme: BrewThemeID { BrewThemeID(rawValue: uiTheme) ?? .dark }
}

extension SettingsModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        learningEnabled = c.lenient(.learningEnabled, true)
        learningFactor = c.lenient(.learningFactor, 0)
        adaptiveLearning = c.lenient(.adaptiveLearning, false)
        minValidWeightG = c.lenient(.minValidWeightG, 0)
        scaleTimeoutMs = c.lenient(.scaleTimeoutMs, 0)
        dripObserveMs = c.lenient(.dripObserveMs, 0)
        minShotTimeMs = c.lenient(.minShotTimeMs, 0)
        noFlowTimeoutS = c.lenient(.noFlowTimeoutS, 0)
        cupMode = c.lenient(.cupMode, 0)
        cupMinWeightG = c.lenient(.cupMinWeightG, 0)
        autoStartAfterTare = c.lenient(.autoStartAfterTare, false)
        scaleTimer = c.lenient(.scaleTimer, false)
        requireCup = c.lenient(.requireCup, false)
        keepPowerBotConnected = c.lenient(.keepPowerBotConnected, false)
        displayFlipped = c.lenient(.displayFlipped, false)
        uiTheme = c.lenient(.uiTheme, 0)
        powerOffWithScale = c.lenient(.powerOffWithScale, false)
        diagnosticsOn = c.lenient(.diagnosticsOn, false)
        namedDrinkers = c.lenient(.namedDrinkers, false)
        webPanels = c.lenient(.webPanels, 0)
        webPanelsChosen = c.lenient(.webPanelsChosen, false)
        scaleRestMin = c.lenient(.scaleRestMin, 0)
        actuationMode = c.lenient(.actuationMode, 0)
        pressDurationMs = c.lenient(.pressDurationMs, 0)
        releaseSettleMs = c.lenient(.releaseSettleMs, 0)
        botHoldSeconds = c.lenient(.botHoldSeconds, 0)
        grindUm = c.lenient(.grindUm, 0)
        grindToleranceS = c.lenient(.grindToleranceS, 0)
        grindUmPerS = c.lenient(.grindUmPerS, 0)
        grindWindow = c.lenient(.grindWindow, 0)
        grindMaxStepUm = c.lenient(.grindMaxStepUm, 0)
        grindTimeBasis = c.lenient(.grindTimeBasis, 0)
        shareEnabled = c.lenient(.shareEnabled, false)
        shareCoffees = c.lenient(.shareCoffees, false)
        shareDialIns = c.lenient(.shareDialIns, false)
        shareShots = c.lenient(.shareShots, false)
        shareRatings = c.lenient(.shareRatings, false)
        grindRatedTarget = c.lenient(.grindRatedTarget, false)
        grindConfidence = c.lenient(.grindConfidence, false)
        grindRoom = c.lenient(.grindRoom, false)
        activeRecipe = c.lenient(.activeRecipe, 0)
        maxRecipes = c.lenient(.maxRecipes, 0)
        recipes = c.lenient(.recipes, [])
        net = c.lenient(.net, SettingsNet())
    }
}

/// One entry of the preset list as /api/settings shows it (the recipe with its coffee and grind).
struct SettingsRecipe: Codable, Sendable, Equatable, Identifiable {
    var slot: Int = 0
    var recipeId: Int = 0
    var coffeeId: Int = 0
    var hidden = false
    var name: String = ""
    var targetWeightG: Double = 0
    var baselineOffsetG: Double = 0
    var stopOffsetG: Double = 0
    var maxShotTimeS: Double = 0
    var timeLimit: Int = 0
    var useRatio = false
    var doseG: Double = 0
    var ratio: Double = 0
    var targetTimeS: Double = 0
    var grindUm: Int = 0
    var coffee: String = ""
    var region: String = ""
    var varietal: String = ""
    var roastLevel: Int = 0
    var roastDate: Int = 0
    var preinfusion: Int = 0
    var preinfusionHoldS: Double = 0
    var bloomPauseS: Double = 0
    var learningFactor: Double = 0

    var id: Int { slot }
}

extension SettingsRecipe {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        slot = c.lenient(.slot, 0)
        recipeId = c.lenient(.recipeId, 0)
        coffeeId = c.lenient(.coffeeId, 0)
        hidden = c.lenient(.hidden, false)
        name = c.lenient(.name, "")
        targetWeightG = c.lenient(.targetWeightG, 0)
        baselineOffsetG = c.lenient(.baselineOffsetG, 0)
        stopOffsetG = c.lenient(.stopOffsetG, 0)
        maxShotTimeS = c.lenient(.maxShotTimeS, 0)
        timeLimit = c.lenient(.timeLimit, 0)
        useRatio = c.lenient(.useRatio, false)
        doseG = c.lenient(.doseG, 0)
        ratio = c.lenient(.ratio, 0)
        targetTimeS = c.lenient(.targetTimeS, 0)
        grindUm = c.lenient(.grindUm, 0)
        coffee = c.lenient(.coffee, "")
        region = c.lenient(.region, "")
        varietal = c.lenient(.varietal, "")
        roastLevel = c.lenient(.roastLevel, 0)
        roastDate = c.lenient(.roastDate, 0)
        preinfusion = c.lenient(.preinfusion, 0)
        preinfusionHoldS = c.lenient(.preinfusionHoldS, 0)
        bloomPauseS = c.lenient(.bloomPauseS, 0)
        learningFactor = c.lenient(.learningFactor, 0)
    }
}

/// The part of the network settings the iPhone app uses (Wi-Fi is the board's; it is not shown).
struct SettingsNet: Codable, Sendable, Equatable {
    var staSsid: String = ""
    /// "Allow Start from Home Assistant" on the iPhone (the MQTT start obeys it).
    var webStartEnabled = false
}

extension SettingsNet {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        staSsid = c.lenient(.staSsid, "")
        webStartEnabled = c.lenient(.webStartEnabled, false)
    }
}
