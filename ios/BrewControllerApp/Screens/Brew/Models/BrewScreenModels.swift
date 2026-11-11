// Small answers the Brew tab reads, decoded leniently (every field optional).
import Foundation

/// One entry of GET /api/contexts?limit=1: the grind advice recorded with a shot.
struct BrewPreviousAdvice: Decodable, Sendable, Equatable {
    var shotId: Int?
    var adviceAction: String?
    var adviceUm: Int?
    var adviceDeltaUm: Int?
    var adviceRefined: Bool?
    var adviceShots: Int?
    var adviceStepLimited: Bool?

    enum CodingKeys: String, CodingKey {
        case shotId = "shot_id", adviceAction = "advice_action", adviceUm = "advice_um"
        case adviceDeltaUm = "advice_delta_um", adviceRefined = "advice_refined", adviceShots = "advice_shots"
        case adviceStepLimited = "advice_step_limited"
    }
}

/// GET /api/community?coffee=&recipe= (Web.cpp handleCommunity).
struct BrewCommunityInfo: Decodable, Sendable, Equatable {
    struct Start: Decodable, Sendable, Equatable {
        var basis: String?
        var setting: Int?
        var range: Int?
        var ageShift: Int?
        var flowShift: Int?

        enum CodingKeys: String, CodingKey {
            case basis, setting, range
            case ageShift = "age_shift", flowShift = "flow_shift"
        }
    }

    /// The owner's own most similar coffee: a guess to start from.
    struct Similar: Decodable, Sendable, Equatable {
        var found: Bool?
        var setting: Int?
        var range: Int?
        var fromCoffee: String?
        var fromSetting: Int?
        var shift: Int?
        var basis: String?
        var samples: Int?
        var applied: Int?
        var hintRoast: Int?
        var hintAltitude: Int?
        var hintProcess: Int?
        var nearRecipe: Bool?

        enum CodingKeys: String, CodingKey {
            case found, setting, range, shift, basis, samples, applied
            case fromCoffee = "from_coffee", fromSetting = "from_setting", hintRoast = "hint_roast"
            case hintAltitude = "hint_altitude", hintProcess = "hint_process", nearRecipe = "near_recipe"
        }
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
    var spreadUm: Double?
    var sameModel: Int?
    var theirAgeDays: Int?
    var coffeesInCommon: Int?
    var myAgeDays: Int?
    var ageRateKnown: Bool?
    var start: Start?
    var similar: Similar?

    enum CodingKeys: String, CodingKey {
        case reachable, found, contributors, dialins, start, similar
        case timeS = "time_s", finenessUm = "fineness_um", axisRelative = "axis_relative"
        case recipeMatch = "recipe_match", theirDoseG = "their_dose_g", theirYieldG = "their_yield_g"
        case spreadUm = "spread_um", sameModel = "same_model", theirAgeDays = "their_age_days"
        case coffeesInCommon = "coffees_in_common", myAgeDays = "my_age_days", ageRateKnown = "age_rate_known"
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

/// GET /api/subscriptions/recommend: the first service the owner does not have yet.
struct BrewSubscriptionRecommendation: Decodable, Sendable, Equatable {
    struct Item: Decodable, Sendable, Equatable {
        struct Service: Decodable, Sendable, Equatable {
            var name: String?
        }
        var service: Service?
    }

    var items: [Item]?
}

/// GET /api/update: the firmware's version.
struct BrewVersionInfo: Decodable, Sendable, Equatable {
    var version: String?
}
