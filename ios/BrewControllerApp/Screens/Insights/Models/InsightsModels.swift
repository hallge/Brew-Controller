// Models for the Insights screen, lenient like Core/Models/LenientDecoding.swift:
//   GET /api/taste/profile?drinker=   (Web.cpp, src/core/CoffeeTaste.h)
//   GET /api/coffee/ratings           (only the drinker names are used here)
//   GET /api/grind/accuracy           (Web.cpp handleGrindAccuracy)
//   GET /api/power/events?days=       (Web.cpp, an array)
// The shots come from Core's ShotModel.
import Foundation

// MARK: - Taste

struct InsightsTasteValues: Codable, Sendable, Equatable {
    /// Coffees and ratings that went into the profile.
    var coffees = 0
    var ratings = 0
    /// The profile shows from 3 rated coffees and is settled from 8.
    var shown = false
    var settled = false
    /// One value per dimension, 0...1 with 0.5 as no lean either way.
    var values: [Double] = []
    /// How well each value is known, 0...1.
    var known: [Double] = []

    enum CodingKeys: String, CodingKey {
        case coffees, ratings, shown, settled, known
        case values = "v"
    }
}

extension InsightsTasteValues {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        coffees = c.lenient(.coffees, 0)
        ratings = c.lenient(.ratings, 0)
        shown = c.lenient(.shown, false)
        settled = c.lenient(.settled, false)
        values = c.lenient(.values, [])
        known = c.lenient(.known, [])
    }
}

/// One coffee of "Liked best".
struct InsightsLikedBest: Codable, Sendable, Equatable {
    var coffee = 0
    var stars = 0

    enum CodingKeys: String, CodingKey { case coffee, stars }
}

extension InsightsLikedBest {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        coffee = c.lenient(.coffee, 0)
        stars = c.lenient(.stars, 0)
    }
}

struct InsightsTasteProfile: Codable, Sendable, Equatable {
    var drinker = 0
    /// Five cup values (acidity, sweetness, body, bitterness, finish), then the eight flavour families.
    var dims: [String] = []
    var profile = InsightsTasteValues()
    var best: [InsightsLikedBest] = []

    enum CodingKeys: String, CodingKey { case drinker, dims, profile, best }
}

extension InsightsTasteProfile {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        drinker = c.lenient(.drinker, 0)
        dims = c.lenient(.dims, [])
        profile = c.lenient(.profile, InsightsTasteValues())
        best = c.lenient(.best, [])
    }

    /// "Acidity" for the dimension's lower-case name.
    func label(_ index: Int) -> String {
        guard dims.indices.contains(index), let first = dims[index].first else { return "" }
        return first.uppercased() + dims[index].dropFirst()
    }

    var cupLabels: [String] { (0..<min(5, dims.count)).map { label($0) } }
    var flavourLabels: [String] { dims.count > 5 ? (5..<dims.count).map { label($0) } : [] }
    var cupValues: [Double] { Array(profile.values.prefix(5)) }
    var cupKnown: [Double] { Array(profile.known.prefix(5)) }
    var flavourValues: [Double] { Array(profile.values.dropFirst(5)) }
    var flavourKnown: [Double] { Array(profile.known.dropFirst(5)) }
}

/// The drinker names of GET /api/coffee/ratings.
struct InsightsRatings: Codable, Sendable, Equatable {
    /// Named drinkers is on.
    var named = false
    /// Eight names, "" for an unused one.
    var drinkers: [String] = []

    enum CodingKeys: String, CodingKey { case named, drinkers }

    /// (drinker number 1...8, name) of the drinkers that have a name.
    var people: [(id: Int, name: String)] {
        guard named else { return [] }
        return drinkers.enumerated().filter { !$0.element.isEmpty }.map { (id: $0.offset + 1, name: $0.element) }
    }

    static func == (lhs: InsightsRatings, rhs: InsightsRatings) -> Bool {
        lhs.named == rhs.named && lhs.drinkers == rhs.drinkers
    }
}

extension InsightsRatings {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        named = c.lenient(.named, false)
        drinkers = c.lenient(.drinkers, [])
    }
}

// MARK: - Grind accuracy

struct InsightsGrindEvent: Codable, Sendable, Equatable {
    /// Unix time of the change.
    var at = 0
    var coffeeId = 0
    /// The settings before, advised and the one the grinder went to (stored steps).
    var from = 0
    var advised = 0
    var next = 0
    /// The grinder went where the advice said.
    var followed = false
    var targetS: Double = 0
    var timeS: Double = 0
    /// The advice's confidence then, percent; nil when not recorded.
    var confidence: Int?

    enum CodingKeys: String, CodingKey {
        case at, from, advised, next, followed, confidence
        case coffeeId = "coffee_id", targetS = "target_s", timeS = "time_s"
    }
}

extension InsightsGrindEvent {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        at = c.lenient(.at, 0)
        coffeeId = c.lenient(.coffeeId, 0)
        from = c.lenient(.from, 0)
        advised = c.lenient(.advised, 0)
        next = c.lenient(.next, 0)
        followed = c.lenient(.followed, false)
        targetS = c.lenient(.targetS, 0)
        timeS = c.lenient(.timeS, 0)
        confidence = c.lenient(.confidence)
    }
}

struct InsightsGrindAccuracy: Codable, Sendable, Equatable {
    var toleranceS: Double = 2
    /// The controller's confidence now, 0...1.
    var score: Double = 0
    var followed = 0
    var rmsS: Double = 0
    var events: [InsightsGrindEvent] = []

    enum CodingKeys: String, CodingKey {
        case score, followed, events
        case toleranceS = "tolerance_s", rmsS = "rms_s"
    }
}

extension InsightsGrindAccuracy {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        toleranceS = c.lenient(.toleranceS, 2)
        score = c.lenient(.score, 0)
        followed = c.lenient(.followed, 0)
        rmsS = c.lenient(.rmsS, 0)
        events = c.lenient(.events, [])
    }
}

// MARK: - Power events

/// One press of the machine's power button, from GET /api/power/events.
struct InsightsPowerEvent: Codable, Sendable, Equatable {
    /// Unix time.
    var t = 0
    var on = false
    /// "schedule", "scale" or "owner".
    var by = ""
    var ok = true
    var sure = true
    var outcome = ""

    enum CodingKeys: String, CodingKey { case t, on, by, ok, sure, outcome }
}

extension InsightsPowerEvent {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        t = c.lenient(.t, 0)
        on = c.lenient(.on, false)
        by = c.lenient(.by, "")
        ok = c.lenient(.ok, true)
        sure = c.lenient(.sure, true)
        outcome = c.lenient(.outcome, "")
    }
}
