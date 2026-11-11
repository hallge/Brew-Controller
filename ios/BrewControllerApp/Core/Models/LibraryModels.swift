// GET /api/library (Web.cpp handleLibrary): coffees, recipes, presets, grinder and bags. Dates are
// YYYYMMDD integers (0: not set). Field names are camelCase here, as the handler writes them.
import Foundation

struct LibraryModel: Codable, Sendable, Equatable {
    var activePreset: Int = 0
    var maxCoffees: Int = 0
    var maxRecipes: Int = 0
    var maxPresets: Int = 0
    var coffees: [CoffeeModel] = []
    var recipes: [RecipeDefModel] = []
    var bags: [BagModel] = []
    var grinder: GrinderModel = GrinderModel()
    /// The second grinder, when the owner has one.
    var otherGrinder: GrinderModel?
    /// The built-in grinder catalogue.
    var grinderModels: [GrinderCatalogEntry] = []
    var presets: [PresetModel] = []
}

extension LibraryModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        activePreset = c.lenient(.activePreset, 0)
        maxCoffees = c.lenient(.maxCoffees, 0)
        maxRecipes = c.lenient(.maxRecipes, 0)
        maxPresets = c.lenient(.maxPresets, 0)
        coffees = c.lenient(.coffees, [])
        recipes = c.lenient(.recipes, [])
        bags = c.lenient(.bags, [])
        grinder = c.lenient(.grinder, GrinderModel())
        otherGrinder = c.lenient(.otherGrinder)
        grinderModels = c.lenient(.grinderModels, [])
        presets = c.lenient(.presets, [])
    }

    /// The preset in use, if the index is valid.
    var activePresetEntry: PresetModel? {
        presets.indices.contains(activePreset) ? presets[activePreset] : nil
    }

    func recipe(id: Int) -> RecipeDefModel? { recipes.first { $0.id == id } }
    func coffee(id: Int) -> CoffeeModel? { coffees.first { $0.id == id } }
}

struct CoffeeModel: Codable, Sendable, Equatable, Identifiable {
    var id: Int = 0
    var name: String = ""
    var roaster: String = ""
    var origin: String = ""
    var region: String = ""
    var producer: String = ""
    var varietal: String = ""
    var process: String = ""
    var notes: String = ""
    var altitudeM: Int = 0
    /// 0 not given, then light to dark as the controller numbers them.
    var roastLevel: Int = 0
    var roastDate: Int = 0
    var frozenDate: Int = 0
    var thawDate: Int = 0
    /// The bag in use.
    var bagId: Int = 0
    /// YYYYMMDD the bag was finished; 0: on the shelf.
    var finished: Int = 0
    /// "local", "pending", "shared", ... (core/Share.h).
    var share: String = "local"
    var shareChanged: Bool = false
    var reviewNote: String = ""
    var libraryId: String = ""

    var isFinished: Bool { finished != 0 }
}

extension CoffeeModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        name = c.lenient(.name, "")
        roaster = c.lenient(.roaster, "")
        origin = c.lenient(.origin, "")
        region = c.lenient(.region, "")
        producer = c.lenient(.producer, "")
        varietal = c.lenient(.varietal, "")
        process = c.lenient(.process, "")
        notes = c.lenient(.notes, "")
        altitudeM = c.lenient(.altitudeM, 0)
        roastLevel = c.lenient(.roastLevel, 0)
        roastDate = c.lenient(.roastDate, 0)
        frozenDate = c.lenient(.frozenDate, 0)
        thawDate = c.lenient(.thawDate, 0)
        bagId = c.lenient(.bagId, 0)
        finished = c.lenient(.finished, 0)
        share = c.lenient(.share, "local")
        shareChanged = c.lenient(.shareChanged, false)
        reviewNote = c.lenient(.reviewNote, "")
        libraryId = c.lenient(.libraryId, "")
    }
}

struct RecipeDefModel: Codable, Sendable, Equatable, Identifiable {
    var id: Int = 0
    var name: String = ""
    var targetWeightG: Double = 0
    var useRatio: Bool = false
    var doseG: Double = 0
    var ratio: Double = 0
    var targetTimeS: Double = 0
    /// 0 machine pre-infusion, 1 hold.
    var preinfusion: Int = 0
    var preinfusionHoldS: Double = 0
    /// Who stops when the target is never reached (the controller's `timeLimit` code).
    var timeLimit: Int = 0
    var maxShotTimeS: Double = 0
    var baselineOffsetG: Double = 0
    var bloomPauseS: Double = 0
}

extension RecipeDefModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        name = c.lenient(.name, "")
        targetWeightG = c.lenient(.targetWeightG, 0)
        useRatio = c.lenient(.useRatio, false)
        doseG = c.lenient(.doseG, 0)
        ratio = c.lenient(.ratio, 0)
        targetTimeS = c.lenient(.targetTimeS, 0)
        preinfusion = c.lenient(.preinfusion, 0)
        preinfusionHoldS = c.lenient(.preinfusionHoldS, 0)
        timeLimit = c.lenient(.timeLimit, 0)
        maxShotTimeS = c.lenient(.maxShotTimeS, 0)
        baselineOffsetG = c.lenient(.baselineOffsetG, 0)
        bloomPauseS = c.lenient(.bloomPauseS, 0)
    }
}

struct BagModel: Codable, Sendable, Equatable, Identifiable {
    var id: Int = 0
    var coffeeId: Int = 0
    var roastDate: Int = 0
    var openedDate: Int = 0
    var frozenDate: Int = 0
    var thawDate: Int = 0
    var weightG: Int = 0
    /// The bag in use.
    var current: Bool = false
    /// Waiting on the shelf, nothing tracked yet.
    var unopened: Bool = false
    /// Ordered, not come yet.
    var onTheWay: Bool = false
    var expectedDate: Int = 0
    /// The subscription it came with, 0: none.
    var subscriptionId: Int = 0
}

extension BagModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        coffeeId = c.lenient(.coffeeId, 0)
        roastDate = c.lenient(.roastDate, 0)
        openedDate = c.lenient(.openedDate, 0)
        frozenDate = c.lenient(.frozenDate, 0)
        thawDate = c.lenient(.thawDate, 0)
        weightG = c.lenient(.weightG, 0)
        current = c.lenient(.current, false)
        unopened = c.lenient(.unopened, false)
        onTheWay = c.lenient(.onTheWay, false)
        expectedDate = c.lenient(.expectedDate, 0)
        subscriptionId = c.lenient(.subscriptionId, 0)
    }
}

/// One entry of the preset list: a recipe with the coffee and grind it is used with.
struct PresetModel: Codable, Sendable, Equatable {
    var recipeId: Int = 0
    var coffeeId: Int = 0
    /// The grinder's stored setting (whole steps; 0: not entered). The name is historical.
    var grindUm: Int = 0
    var stopOffsetG: Double = 0
    var learnedAlpha: Double = 0
    /// Its coffee's bag is finished.
    var hidden: Bool = false
}

extension PresetModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        recipeId = c.lenient(.recipeId, 0)
        coffeeId = c.lenient(.coffeeId, 0)
        grindUm = c.lenient(.grindUm, 0)
        stopOffsetG = c.lenient(.stopOffsetG, 0)
        learnedAlpha = c.lenient(.learnedAlpha, 0)
        hidden = c.lenient(.hidden, false)
    }
}

/// The owner's grinder (and the second one).
struct GrinderModel: Codable, Sendable, Equatable {
    var id: Int = 0
    var modelId: Int = 0
    var model: String = ""
    var burrs: String = ""
    var unit: String = "um"
    /// Microns per STORED step; 0: not known.
    var umPerUnit: Double = 1
    /// Stored steps per displayed unit.
    var divisor: Int = 1
    /// 0, or the inner ring's steps per outer step.
    var subSteps: Int = 0
    /// Range in stored steps, 0: not given.
    var min: Int = 0
    var max: Int = 0
    /// 0 not given, 1 espresso, 2 espresso to filter.
    var covers: Int = 0
    var zeroDate: Int = 0
    var calibration: Int = 0
    var share: String = "local"
    var shareChanged: Bool = false
    var canShare: Bool = false
    var reviewNote: String = ""
    var libraryId: String = ""

    /// How this grinder's settings are written (see `BrewFormat.Dial`).
    var dial: BrewFormat.Dial { BrewFormat.Dial(unit: unit, divisor: divisor, subSteps: subSteps) }
}

extension GrinderModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        modelId = c.lenient(.modelId, 0)
        model = c.lenient(.model, "")
        burrs = c.lenient(.burrs, "")
        unit = c.lenient(.unit, "um")
        umPerUnit = c.lenient(.umPerUnit, 1)
        divisor = c.lenient(.divisor, 1)
        subSteps = c.lenient(.subSteps, 0)
        min = c.lenient(.min, 0)
        max = c.lenient(.max, 0)
        covers = c.lenient(.covers, 0)
        zeroDate = c.lenient(.zeroDate, 0)
        calibration = c.lenient(.calibration, 0)
        share = c.lenient(.share, "local")
        shareChanged = c.lenient(.shareChanged, false)
        canShare = c.lenient(.canShare, false)
        reviewNote = c.lenient(.reviewNote, "")
        libraryId = c.lenient(.libraryId, "")
    }
}

/// One built-in grinder model (the catalogue in `grinderModels`).
struct GrinderCatalogEntry: Codable, Sendable, Equatable, Identifiable {
    var id: Int = 0
    var model: String = ""
    var burrs: String = ""
    var unit: String = ""
    var umPerUnit: Double = 0
    var divisor: Int = 1
    var subSteps: Int = 0
    /// Only when the model's range is known.
    var min: Int?
    var max: Int?
    var libraryId: String = ""
}

extension GrinderCatalogEntry {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        model = c.lenient(.model, "")
        burrs = c.lenient(.burrs, "")
        unit = c.lenient(.unit, "")
        umPerUnit = c.lenient(.umPerUnit, 0)
        divisor = c.lenient(.divisor, 1)
        subSteps = c.lenient(.subSteps, 0)
        min = c.lenient(.min)
        max = c.lenient(.max)
        libraryId = c.lenient(.libraryId, "")
    }
}
