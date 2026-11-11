// Small answers the Brew tab reads, decoded leniently (every field optional).
import Foundation

// MARK: - Small answers the Brew tab reads (decoded leniently: every field optional)

/// One entry of GET /api/contexts?limit=1: the grind advice recorded with a shot.
struct BrewPreviousAdvice: Decodable, Sendable, Equatable {
    var shotId: Int?
    var adviceAction: String?
    var adviceUm: Int?
    var adviceDeltaUm: Int?
    var adviceRefined: Bool?
    var adviceShots: Int?

    enum CodingKeys: String, CodingKey {
        case shotId = "shot_id", adviceAction = "advice_action", adviceUm = "advice_um"
        case adviceDeltaUm = "advice_delta_um", adviceRefined = "advice_refined", adviceShots = "advice_shots"
    }
}

/// GET /api/community?coffee=&recipe= (Web.cpp handleCommunity).
struct BrewCommunityInfo: Decodable, Sendable, Equatable {
    struct Start: Decodable, Sendable, Equatable {
        var basis: String?
        var setting: Int?
        var range: Int?
    }

    var reachable: Bool?
    var found: Bool?
    var contributors: Int?
    var dialins: Int?
    var timeS: Double?
    var finenessUm: Double?
    var axisRelative: Bool?
    var recipeMatch: String?
    var theirDoseG: Double?
    var theirYieldG: Double?
    var start: Start?

    enum CodingKeys: String, CodingKey {
        case reachable, found, contributors, dialins, start
        case timeS = "time_s", finenessUm = "fineness_um", axisRelative = "axis_relative"
        case recipeMatch = "recipe_match", theirDoseG = "their_dose_g", theirYieldG = "their_yield_g"
    }
}

/// GET /api/taste/recommend (the web page's "For you").
struct BrewTasteRecommendation: Decodable, Sendable, Equatable {
    struct Item: Decodable, Sendable, Equatable {
        var name: String?
        var roaster: String?
        var origin: String?
        var from: String?
        var why: [String]?
    }

    var shown: Bool?
    var coffees: Int?
    var items: [Item]?
}

