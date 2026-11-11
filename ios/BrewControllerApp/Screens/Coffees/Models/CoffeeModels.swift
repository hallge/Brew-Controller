// Models for the Coffees screens that Core/Models does not have: the ratings, the community dial-in, the
// directory and the shared library, and what the sharing routes answer. Lenient like
// Core/Models/LenientDecoding.swift: a missing or oddly typed field falls back to a default.
import Foundation

// MARK: - Replies

/// `{"ok":true,"id":N}` (POST /api/coffee, /api/bag, /api/coffee/adopt, /api/coffee/rate).
struct CoffeeIdReply: Codable, Sendable, Equatable {
    var ok: Bool = true
    var id: Int = 0

    init(ok: Bool = true, id: Int = 0) {
        self.ok = ok
        self.id = id
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        ok = c.lenient(.ok, true)
        id = c.lenient(.id, 0)
    }
}

// MARK: - Ratings (GET /api/coffee/ratings)

/// One rating of a coffee's bag: how the drinker liked it.
struct CoffeeRatingEntry: Codable, Sendable, Equatable, Identifiable {
    var id: Int = 0
    var coffee: Int = 0
    var bag: Int = 0
    /// 0 is the household; 1...8 a named drinker.
    var drinker: Int = 0
    /// Unix seconds.
    var t: Int = 0
    var stars: Int = 0
    /// Acidity, sweetness, body, bitterness, finish; 0 is not given.
    var attr: [Int] = [0, 0, 0, 0, 0]
    /// Bit i set: flavour family i (see `CoffeeRatingText.families`).
    var flavours: Int = 0
    var share: String = "local"

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        coffee = c.lenient(.coffee, 0)
        bag = c.lenient(.bag, 0)
        drinker = c.lenient(.drinker, 0)
        t = c.lenient(.t, 0)
        stars = c.lenient(.stars, 0)
        var values: [Int] = c.lenient(.attr, [])
        while values.count < 5 { values.append(0) }
        attr = Array(values.prefix(5))
        flavours = c.lenient(.flavours, 0)
        share = c.lenient(.share, "local")
    }
}

struct CoffeeRatingsModel: Codable, Sendable, Equatable {
    /// Named drinkers are on (Setup): each rating says who gave it.
    var named: Bool = false
    /// The eight names; "" is not set.
    var drinkers: [String] = []
    var attrs: [String] = []
    var families: [String] = []
    var ratings: [CoffeeRatingEntry] = []

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        named = c.lenient(.named, false)
        drinkers = c.lenient(.drinkers, [])
        attrs = c.lenient(.attrs, [])
        families = c.lenient(.families, [])
        ratings = c.lenient(.ratings, [])
    }

    /// The (number, name) pairs of the named drinkers: 1...8, only those with a name.
    var people: [(id: Int, name: String)] {
        drinkers.enumerated().compactMap { index, name in
            name.isEmpty ? nil : (id: index + 1, name: name)
        }
    }

    /// A drinker's name: "Household" for 0, "Drinker n" when the name is not set.
    func drinkerName(_ id: Int) -> String {
        guard id > 0 else { return "Household" }
        if id - 1 < drinkers.count, !drinkers[id - 1].isEmpty { return drinkers[id - 1] }
        return "Drinker \(id)"
    }

    /// The rating one drinker gave a coffee's bag, if any.
    func rating(coffee: Int, bag: Int, drinker: Int) -> CoffeeRatingEntry? {
        ratings.first { $0.coffee == coffee && $0.bag == bag && $0.drinker == drinker }
    }
}

/// The words of the rating card (WebPage.h ATTR_LABEL, FAM_LABEL).
enum CoffeeRatingText {
    static let attributes = ["Acidity", "Sweetness", "Body", "Bitterness", "Finish"]
    static let families = ["Fruity", "Floral", "Sweet / caramel", "Chocolate / nutty", "Spicy", "Roasty / smoky",
                           "Fermented / winey", "Earthy / green"]
}

// MARK: - Community dial-in (GET /api/community?coffee=&recipe=)

struct CoffeeCommunityStart: Codable, Sendable, Equatable {
    /// "fitted", "same_model", "model", "microns" or "none".
    var basis: String = "none"
    var setting: Int = 0
    var range: Int = 0
    var ageShift: Int = 0
    var flowShift: Int = 0

    init() {}

    enum CodingKeys: String, CodingKey {
        case basis, setting, range
        case ageShift = "age_shift", flowShift = "flow_shift"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        basis = c.lenient(.basis, "none")
        setting = c.lenient(.setting, 0)
        range = c.lenient(.range, 0)
        ageShift = c.lenient(.ageShift, 0)
        flowShift = c.lenient(.flowShift, 0)
    }
}

struct CoffeeCommunitySimilar: Codable, Sendable, Equatable {
    var found: Bool = false
    var setting: Int = 0
    var range: Int = 0
    var fromCoffee: String = ""
    var fromSetting: Int = 0
    var shift: Int = 0
    /// "fitted" or "nearest".
    var basis: String = ""

    init() {}

    enum CodingKeys: String, CodingKey {
        case found, setting, range, shift, basis
        case fromCoffee = "from_coffee", fromSetting = "from_setting"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        found = c.lenient(.found, false)
        setting = c.lenient(.setting, 0)
        range = c.lenient(.range, 0)
        fromCoffee = c.lenient(.fromCoffee, "")
        fromSetting = c.lenient(.fromSetting, 0)
        shift = c.lenient(.shift, 0)
        basis = c.lenient(.basis, "")
    }
}

struct CoffeeCommunityModel: Codable, Sendable, Equatable {
    var reachable: Bool = false
    var found: Bool = false
    var contributors: Int = 0
    var dialins: Int = 0
    var timeS: Double = 0
    var spreadUm: Double = 0
    var sameModel: Int = 0
    var theirAgeDays: Int = -1
    var theirDoseG: Double = 0
    var theirYieldG: Double = 0
    /// "exact" or "near".
    var recipeMatch: String = "exact"
    var coffeesInCommon: Int = 0
    var start: CoffeeCommunityStart = CoffeeCommunityStart()
    var similar: CoffeeCommunitySimilar = CoffeeCommunitySimilar()
    var myAgeDays: Int = -1
    var ageRateKnown: Bool = false

    init() {}

    enum CodingKeys: String, CodingKey {
        case reachable, found, contributors, dialins, start, similar
        case timeS = "time_s", spreadUm = "spread_um", sameModel = "same_model", theirAgeDays = "their_age_days"
        case theirDoseG = "their_dose_g", theirYieldG = "their_yield_g", recipeMatch = "recipe_match"
        case coffeesInCommon = "coffees_in_common", myAgeDays = "my_age_days", ageRateKnown = "age_rate_known"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        reachable = c.lenient(.reachable, false)
        found = c.lenient(.found, false)
        contributors = c.lenient(.contributors, 0)
        dialins = c.lenient(.dialins, 0)
        timeS = c.lenient(.timeS, 0)
        spreadUm = c.lenient(.spreadUm, 0)
        sameModel = c.lenient(.sameModel, 0)
        theirAgeDays = c.lenient(.theirAgeDays, -1)
        theirDoseG = c.lenient(.theirDoseG, 0)
        theirYieldG = c.lenient(.theirYieldG, 0)
        recipeMatch = c.lenient(.recipeMatch, "exact")
        coffeesInCommon = c.lenient(.coffeesInCommon, 0)
        start = c.lenient(.start, CoffeeCommunityStart())
        similar = c.lenient(.similar, CoffeeCommunitySimilar())
        myAgeDays = c.lenient(.myAgeDays, -1)
        ageRateKnown = c.lenient(.ageRateKnown, false)
    }
}

// MARK: - Directory and shared library (GET /api/directory, /api/community/coffees)

/// One coffee of the shared library: as the directory lists it and as `/api/community/coffees` has it.
struct CoffeeLibraryEntry: Codable, Sendable, Equatable, Identifiable {
    var id: String = ""
    var name: String = ""
    var roaster: String = ""
    var origin: String = ""
    var region: String = ""
    var producer: String = ""
    var varietal: String = ""
    var process: String = ""
    var notes: String = ""
    /// "light", "medium-light", "medium", "medium-dark", "dark" or "".
    var roastLevel: String = ""
    var altitudeM: Int = 0
    /// The library's average, 0: not rated.
    var stars: Double = 0
    var ratings: Int = 0
    /// Directory only: already among the owner's coffees.
    var onShelf: Bool = false

    init() {}

    enum CodingKeys: String, CodingKey {
        case id, name, roaster, origin, region, producer, varietal, process, notes, stars
        case roastLevel = "roast_level", altitudeM = "altitude_m", ratings = "n", onShelf = "on_shelf"
    }

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
        stars = c.lenient(.stars, 0)
        ratings = c.lenient(.ratings, 0)
        onShelf = c.lenient(.onShelf, false)
    }

    /// "Portrait · Ethiopia · washed": what is known, in the order the web page writes it.
    var summary: String {
        [roaster, origin, process].filter { !$0.isEmpty }.joined(separator: " · ")
    }

    /// The roast level number (1...5) the controller uses; 0 when not given.
    var roastLevelNumber: Int {
        CoffeeRoast.names.firstIndex(of: roastLevel) ?? 0
    }
}

/// `{"v":1,"coffees":[...]}`
struct CoffeeSharedLibraryModel: Codable, Sendable, Equatable {
    var coffees: [CoffeeLibraryEntry] = []

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        coffees = c.lenient(.coffees, [])
    }
}

struct CoffeeDirectoryFacets: Codable, Sendable, Equatable {
    var origin: [String: Int] = [:]
    var process: [String: Int] = [:]
    var roastLevel: [String: Int] = [:]

    init() {}

    enum CodingKeys: String, CodingKey {
        case origin, process
        case roastLevel = "roast_level"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        origin = c.lenient(.origin, [:])
        process = c.lenient(.process, [:])
        roastLevel = c.lenient(.roastLevel, [:])
    }
}

struct CoffeeDirectoryModel: Codable, Sendable, Equatable {
    var coffees: [CoffeeLibraryEntry] = []
    var matched: Int = 0
    var page: Int = 0
    var pageSize: Int = 20
    /// 0: the directory has not been read from the shared library yet.
    var keptPages: Int = 0
    var facets: CoffeeDirectoryFacets?
    var count: Int = 0

    init() {}

    enum CodingKeys: String, CodingKey {
        case coffees, matched, page, facets, count
        case pageSize = "page_size", keptPages = "kept_pages"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        coffees = c.lenient(.coffees, [])
        matched = c.lenient(.matched, 0)
        page = c.lenient(.page, 0)
        pageSize = c.lenient(.pageSize, 20)
        keptPages = c.lenient(.keptPages, 0)
        facets = c.lenient(.facets)
        count = c.lenient(.count, 0)
    }

    var pageCount: Int { pageSize > 0 ? (matched + pageSize - 1) / pageSize : 0 }
}

// MARK: - Share status (GET /api/share)

struct CoffeeShareModel: Codable, Sendable, Equatable {
    var keyReady: Bool = false
    var canSend: Bool = false
    /// false: what is "sent" goes no further than this device.
    var toLibrary: Bool = false
    var via: String = ""
    var problem: String = ""
    var waiting: Int = 0
    var sent: Int = 0

    init() {}

    enum CodingKeys: String, CodingKey {
        case via, problem, waiting, sent
        case keyReady = "key_ready", canSend = "can_send", toLibrary = "to_library"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        keyReady = c.lenient(.keyReady, false)
        canSend = c.lenient(.canSend, false)
        toLibrary = c.lenient(.toLibrary, false)
        via = c.lenient(.via, "")
        problem = c.lenient(.problem, "")
        waiting = c.lenient(.waiting, 0)
        sent = c.lenient(.sent, 0)
    }
}

// MARK: - Words and dates

/// The roast levels of the form: 0 not set, then light to dark (WebPage.h RL).
enum CoffeeRoast {
    /// Index = the controller's number; "" for 0.
    static let names = ["", "light", "medium-light", "medium", "medium-dark", "dark"]

    static func label(_ level: Int) -> String {
        level > 0 && level < names.count ? names[level] : "not set"
    }
}

/// The words of a share state (WebPage.h SHN).
enum CoffeeShareText {
    static func state(_ share: String) -> String {
        switch share {
        case "local": return "on this controller only"
        case "queued": return "waiting to send"
        case "sent": return "sent"
        case "review": return "in review"
        case "published": return "published"
        case "declined": return "declined"
        case "refused": return "refused by the library"
        default: return share
        }
    }
}

/// YYYYMMDD integers (how the controller stores dates) and the dates the pickers use.
enum CoffeeDates {
    /// A picker date as YYYYMMDD, in the phone's calendar day.
    static func ymd(_ date: Date) -> Int {
        let parts = Calendar(identifier: .gregorian).dateComponents([.year, .month, .day], from: date)
        return (parts.year ?? 0) * 10_000 + (parts.month ?? 0) * 100 + (parts.day ?? 0)
    }

    /// YYYYMMDD as a date; nil for 0 or an invalid number.
    static func date(_ ymd: Int) -> Date? { BrewFormat.date(fromYMD: ymd) }

    static func today() -> Int { ymd(Date()) }

    /// "1 Nov 2026" for a stored date; "" when 0.
    static func text(_ ymd: Int) -> String {
        guard let date = date(ymd) else { return "" }
        return date.formatted(date: .abbreviated, time: .omitted)
    }

    /// Whole days from a stored date to today; nil when not set or in the future.
    static func daysSince(_ ymd: Int) -> Int? {
        guard let date = date(ymd) else { return nil }
        let days = Calendar(identifier: .gregorian).dateComponents([.day], from: date, to: Date()).day ?? 0
        return days >= 0 ? days : nil
    }
}
