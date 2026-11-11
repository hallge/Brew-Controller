// Models for the Discover screen, lenient like Core/Models/LenientDecoding.swift. Field names from the
// web page's use of the routes (src/net/WebPage.h) and src/net/Web.cpp:
//   GET /api/taste/recommend?drinker=   DiscoverRecommendations
//   GET /api/community/coffees          DiscoverCoffeeIndex   (the cached library index; {"coffees":[]} offline)
//   GET /api/community/recipes          DiscoverRecipeBook    ({} offline)
//   GET /api/library/pending            DiscoverPending
//   POST /api/library/review            {target, verdict: approve|decline, note}
// The relay is not reachable in demo mode, so the published and pending lists are empty there: the
// fixtures for them (Fixtures/discover_*_published.json) are written by hand from what the web page reads.
import Foundation

// MARK: - Coffees you might like

struct DiscoverRecoItem: Codable, Sendable, Equatable, Identifiable {
    var name = ""
    var roaster = ""
    var origin = ""
    var process = ""
    var key = ""
    /// The coffee on the shelf (0: a library coffee you do not have).
    var coffee = 0
    /// "shelf", "library" or "community".
    var from = ""
    var score: Double = 0
    /// How sure the controller is, 0...1 (below 0.4: a guess from its description).
    var sure: Double = 0
    var why: [String] = []
    /// A coffee of yours it is like, and the stars you gave it.
    var like = 0
    var likeStars = 0
    var peopleStars: Double = 0
    var communityStars: Double = 0
    var communityN = 0

    var id: String { key.isEmpty ? name : key }

    enum CodingKeys: String, CodingKey {
        case name, roaster, origin, process, key, coffee, from, score, sure, why, like
        case likeStars = "like_stars", peopleStars = "people_stars", communityStars = "community_stars"
        case communityN = "community_n"
    }
}

extension DiscoverRecoItem {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = c.lenient(.name, "")
        roaster = c.lenient(.roaster, "")
        origin = c.lenient(.origin, "")
        process = c.lenient(.process, "")
        key = c.lenient(.key, "")
        coffee = c.lenient(.coffee, 0)
        from = c.lenient(.from, "")
        score = c.lenient(.score, 0)
        sure = c.lenient(.sure, 0)
        why = c.lenient(.why, [])
        like = c.lenient(.like, 0)
        likeStars = c.lenient(.likeStars, 0)
        peopleStars = c.lenient(.peopleStars, 0)
        communityStars = c.lenient(.communityStars, 0)
        communityN = c.lenient(.communityN, 0)
    }
}

struct DiscoverBuyAgain: Codable, Sendable, Equatable, Identifiable {
    var name = ""
    var stars = 0
    var coffee = 0
    var id: String { "\(coffee)-\(name)" }

    enum CodingKeys: String, CodingKey { case name, stars, coffee }
}

extension DiscoverBuyAgain {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = c.lenient(.name, "")
        stars = c.lenient(.stars, 0)
        coffee = c.lenient(.coffee, 0)
    }
}

struct DiscoverRecommendations: Codable, Sendable, Equatable {
    var drinker = 0
    /// Recommendations show once 3 coffees are rated.
    var shown = false
    /// Coffees rated so far.
    var coffees = 0
    var items: [DiscoverRecoItem] = []
    var buyAgain: [DiscoverBuyAgain] = []

    enum CodingKeys: String, CodingKey {
        case drinker, shown, coffees, items
        case buyAgain = "buy_again"
    }
}

extension DiscoverRecommendations {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        drinker = c.lenient(.drinker, 0)
        shown = c.lenient(.shown, false)
        coffees = c.lenient(.coffees, 0)
        items = c.lenient(.items, [])
        buyAgain = c.lenient(.buyAgain, [])
    }
}

// MARK: - The coffee library

/// A coffee of the shared library (the index, and inside a pending item).
struct DiscoverLibraryCoffee: Codable, Sendable, Equatable, Identifiable {
    var id = ""
    var name = ""
    var roaster = ""
    var origin = ""
    var region = ""
    var producer = ""
    var varietal = ""
    var process = ""
    var notes = ""
    /// "light", "medium-light", "medium", "medium-dark" or "dark"; "" not given.
    var roastLevel = ""
    var altitudeM = 0

    enum CodingKeys: String, CodingKey {
        case id, name, roaster, origin, region, producer, varietal, process, notes
        case roastLevel = "roast_level", altitudeM = "altitude_m"
    }

    /// The body for POST /api/coffee/adopt (web page adopt()).
    var adoptJSON: [String: BrewJSON] {
        [
            "libraryId": .string(id), "name": .string(name), "roaster": .string(roaster),
            "origin": .string(origin), "region": .string(region), "producer": .string(producer),
            "varietal": .string(varietal), "process": .string(process), "notes": .string(notes),
            "roastLevel": .int(max(0, InsightsMath.roastIndex(roastLevel))), "altitudeM": .int(altitudeM),
        ]
    }

    /// "Demo Roasters, Ethiopia, Natural".
    var detail: String { [roaster, origin, process].filter { !$0.isEmpty }.joined(separator: ", ") }
}

extension DiscoverLibraryCoffee {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, "")
        name = c.lenient(.name, "")
        roaster = c.lenient(.roaster, "")
        origin = c.lenient(.origin, "")
        region = c.lenient(.region, "")
        producer = c.lenient(.producer, "")
        varietal = c.lenient(.varietal, "")
        process = c.lenient(.process, "")
        notes = c.lenient(.notes, "")
        roastLevel = c.lenient(.roastLevel, "")
        altitudeM = c.lenient(.altitudeM, 0)
    }
}

struct DiscoverCoffeeIndex: Codable, Sendable, Equatable {
    var coffees: [DiscoverLibraryCoffee] = []

    enum CodingKeys: String, CodingKey { case coffees }
}

extension DiscoverCoffeeIndex {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        coffees = c.lenient(.coffees, [])
    }
}

// MARK: - The recipe book

/// A recipe of the book or of a pending item.
struct DiscoverRecipe: Codable, Sendable, Equatable {
    var name = ""
    var doseG: Double = 0
    var yieldG: Double = 0
    var targetTimeS: Double = 0
    var preinfusionHoldS: Double = 0
    var bloomPauseS: Double = 0

    enum CodingKeys: String, CodingKey {
        case name
        case doseG = "dose_g", yieldG = "yield_g", targetTimeS = "target_time_s"
        case preinfusionHoldS = "preinfusion_hold_s", bloomPauseS = "bloom_pause_s"
    }

    /// "18 g in, 36 g out, 28 s, hold 3 s, bloom 5 s" (the web page's recipeLine).
    var line: String {
        func number(_ value: Double) -> String {
            let rounded = (value * 10).rounded() / 10
            return rounded == rounded.rounded() ? String(Int(rounded)) : BrewFormat.decimal(rounded, 1)
        }
        var text = "\(number(doseG)) g in, \(number(yieldG)) g out"
        if targetTimeS > 0 { text += ", \(number(targetTimeS)) s" }
        if preinfusionHoldS > 0 { text += ", hold \(number(preinfusionHoldS)) s" }
        if bloomPauseS > 0 { text += ", bloom \(number(bloomPauseS)) s" }
        return text
    }
}

extension DiscoverRecipe {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = c.lenient(.name, "")
        doseG = c.lenient(.doseG, 0)
        yieldG = c.lenient(.yieldG, 0)
        targetTimeS = c.lenient(.targetTimeS, 0)
        preinfusionHoldS = c.lenient(.preinfusionHoldS, 0)
        bloomPauseS = c.lenient(.bloomPauseS, 0)
    }
}

struct DiscoverRecipeEntry: Codable, Sendable, Equatable, Identifiable {
    var recipe = DiscoverRecipe()
    var contributors = 0
    var id: String { recipe.name + "|" + recipe.line }

    enum CodingKeys: String, CodingKey { case recipe, contributors }
}

extension DiscoverRecipeEntry {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        recipe = c.lenient(.recipe, DiscoverRecipe())
        contributors = c.lenient(.contributors, 0)
    }
}

struct DiscoverRecipeBook: Codable, Sendable, Equatable {
    var items: [DiscoverRecipeEntry] = []

    enum CodingKeys: String, CodingKey { case items }
}

extension DiscoverRecipeBook {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        items = c.lenient(.items, [])
    }
}

// MARK: - Waiting for review

/// One item waiting for owners' review.
struct DiscoverPendingItem: Codable, Sendable, Equatable, Identifiable {
    var id = ""
    /// "coffee" or "recipe".
    var type = ""
    /// The key id of the owner who sent it.
    var from = ""
    var coffee = DiscoverLibraryCoffee()
    var recipe = DiscoverRecipe()
    var approvals = 0
    var declines = 0

    enum CodingKeys: String, CodingKey { case id, type, from, coffee, recipe, approvals, declines }
}

extension DiscoverPendingItem {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, "")
        type = c.lenient(.type, "")
        from = c.lenient(.from, "")
        coffee = c.lenient(.coffee, DiscoverLibraryCoffee())
        recipe = c.lenient(.recipe, DiscoverRecipe())
        approvals = c.lenient(.approvals, 0)
        declines = c.lenient(.declines, 0)
    }
}

/// Your own review of an item: approved or declined.
struct DiscoverMyReview: Codable, Sendable, Equatable {
    var target = ""
    var approve = false

    enum CodingKeys: String, CodingKey { case target, approve }
}

extension DiscoverMyReview {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        target = c.lenient(.target, "")
        approve = c.lenient(.approve, false)
    }
}

struct DiscoverPendingIndex: Codable, Sendable, Equatable {
    var items: [DiscoverPendingItem] = []

    enum CodingKeys: String, CodingKey { case items }
}

extension DiscoverPendingIndex {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        items = c.lenient(.items, [])
    }
}

struct DiscoverPending: Codable, Sendable, Equatable {
    /// This controller's key id.
    var kid = ""
    /// The shared library is linked.
    var linked = false
    var sharing = false
    /// Reviewing is open: something you shared has been published.
    var canReview = false
    var mine: [DiscoverMyReview] = []
    var index = DiscoverPendingIndex()

    enum CodingKeys: String, CodingKey {
        case kid, linked, sharing, mine, index
        case canReview = "can_review"
    }

    func myVerdict(for target: String) -> Bool? {
        mine.first { $0.target == target }?.approve
    }
}

extension DiscoverPending {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        kid = c.lenient(.kid, "")
        linked = c.lenient(.linked, false)
        sharing = c.lenient(.sharing, false)
        canReview = c.lenient(.canReview, false)
        mine = c.lenient(.mine, [])
        index = c.lenient(.index, DiscoverPendingIndex())
    }
}
