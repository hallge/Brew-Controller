// Models for the Community screen, lenient like Core/Models/LenientDecoding.swift:
//   GET  /api/links                    CommunityLinks      (the feed and your suggestions)
//   POST /api/links/suggest            {url, title, kind, topics[], why, image}
//   POST /api/links/delete?id=
//   GET  /api/trending                 CommunityTrending   ({} offline)
//   GET  /api/community/grinders       CommunityGrinderList
//   GET  /api/share                    CommunityShareStatus
//   GET  /api/community?coffee=&recipe= CommunityNow
// With no relay (demo mode) the feed, trending and grinder lists are empty or null; the fixtures for
// their filled shapes (Fixtures/community_*_published.json) are written by hand from what the web page reads.
import Foundation

// MARK: - The feed

/// A link in the feed that a reviewer approved.
struct CommunityLink: Codable, Sendable, Equatable, Identifiable {
    var url = ""
    var title = ""
    /// "video", "playlist", "post", "guide", "article" or "podcast".
    var kind = ""
    var topics: [String] = []
    var why = ""
    var site = ""
    var image = ""
    /// When it was added, as the library writes it ("2026-11-02").
    var added = ""
    var id: String { url }

    enum CodingKeys: String, CodingKey { case url, title, kind, topics, why, site, image, added }
}

extension CommunityLink {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        url = c.lenient(.url, "")
        title = c.lenient(.title, "")
        kind = c.lenient(.kind, "")
        topics = c.lenient(.topics, [])
        why = c.lenient(.why, "")
        site = c.lenient(.site, "")
        image = c.lenient(.image, "")
        added = c.lenient(.added, "")
    }
}

/// A suggestion of yours.
struct CommunitySuggestion: Codable, Sendable, Equatable, Identifiable {
    var id = 0
    var at = 0
    var url = ""
    var title = ""
    var kind = ""
    var topics: [String] = []
    var why = ""
    var image = ""
    /// "local", "queued", "sent", "review", "published", "declined" or "refused".
    var state = "local"
    var note = ""
    var libraryId = ""

    enum CodingKeys: String, CodingKey {
        case id, at, url, title, kind, topics, why, image, state, note
        case libraryId = "library_id"
    }
}

extension CommunitySuggestion {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        at = c.lenient(.at, 0)
        url = c.lenient(.url, "")
        title = c.lenient(.title, "")
        kind = c.lenient(.kind, "")
        topics = c.lenient(.topics, [])
        why = c.lenient(.why, "")
        image = c.lenient(.image, "")
        state = c.lenient(.state, "local")
        note = c.lenient(.note, "")
        libraryId = c.lenient(.libraryId, "")
    }
}

struct CommunityLinks: Codable, Sendable, Equatable {
    /// The approved links (null offline: no list yet).
    var listed: [CommunityLink] = []
    var mine: [CommunitySuggestion] = []
    var canSend = false

    enum CodingKeys: String, CodingKey {
        case listed, mine
        case canSend = "can_send"
    }
}

extension CommunityLinks {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        listed = c.lenient(.listed, [])
        mine = c.lenient(.mine, [])
        canSend = c.lenient(.canSend, false)
    }
}

// MARK: - Trending

struct CommunityTrendingRecipe: Codable, Sendable, Equatable, Identifiable {
    var doseG: Double = 0
    var yieldG: Double = 0
    var ratio: Double = 0
    var timeS: Double = 0
    /// "straight", "preinfusion" or "bloom".
    var method = ""
    var contributors = 0
    var dialins = 0
    var coffees = 0
    var id: String { "\(doseG)-\(yieldG)-\(timeS)-\(method)" }

    enum CodingKeys: String, CodingKey {
        case ratio, method, contributors, dialins, coffees
        case doseG = "dose_g", yieldG = "yield_g", timeS = "time_s"
    }
}

extension CommunityTrendingRecipe {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        doseG = c.lenient(.doseG, 0)
        yieldG = c.lenient(.yieldG, 0)
        ratio = c.lenient(.ratio, 0)
        timeS = c.lenient(.timeS, 0)
        method = c.lenient(.method, "")
        contributors = c.lenient(.contributors, 0)
        dialins = c.lenient(.dialins, 0)
        coffees = c.lenient(.coffees, 0)
    }
}

struct CommunityTrending: Codable, Sendable, Equatable {
    /// The window the list covers, in days (0: not given).
    var days = 0
    var recipes: [CommunityTrendingRecipe] = []

    enum CodingKeys: String, CodingKey { case days, recipes }
}

extension CommunityTrending {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        days = c.lenient(.days, 0)
        recipes = c.lenient(.recipes, [])
    }
}

// MARK: - Grinders

struct CommunityGrinder: Codable, Sendable, Equatable, Identifiable {
    var model = ""
    var burrs = ""
    var unit = ""
    var id: String { model + "|" + burrs }

    enum CodingKeys: String, CodingKey { case model, burrs, unit }
}

extension CommunityGrinder {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        model = c.lenient(.model, "")
        burrs = c.lenient(.burrs, "")
        unit = c.lenient(.unit, "")
    }
}

struct CommunityGrinderList: Codable, Sendable, Equatable {
    var grinders: [CommunityGrinder] = []

    enum CodingKeys: String, CodingKey { case grinders }
}

extension CommunityGrinderList {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        grinders = c.lenient(.grinders, [])
    }
}

// MARK: - What you share

struct CommunityShareStatus: Codable, Sendable, Equatable {
    var keyReady = false
    var keyId = ""
    var canSend = false
    /// Why the library could not be reached ("" none).
    var problem = ""
    /// Unix time the library last answered (0: never).
    var lastOk = 0
    var waiting = 0
    var sent = 0
    var capacity = 0

    enum CodingKeys: String, CodingKey {
        case problem, waiting, sent, capacity
        case keyReady = "key_ready", keyId = "key_id", canSend = "can_send", lastOk = "last_ok"
    }
}

extension CommunityShareStatus {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        keyReady = c.lenient(.keyReady, false)
        keyId = c.lenient(.keyId, "")
        canSend = c.lenient(.canSend, false)
        problem = c.lenient(.problem, "")
        lastOk = c.lenient(.lastOk, 0)
        waiting = c.lenient(.waiting, 0)
        sent = c.lenient(.sent, 0)
        capacity = c.lenient(.capacity, 0)
    }
}

// MARK: - For what you are brewing

/// Where to start the grinder, from what other owners settled on.
struct CommunityStart: Codable, Sendable, Equatable {
    /// "fitted", "same_model", "model", "microns" or "none".
    var basis = "none"
    /// Stored steps; 0: not known.
    var setting = 0
    var range = 0
    var ageShift = 0
    var flowShift = 0

    enum CodingKeys: String, CodingKey {
        case basis, setting, range
        case ageShift = "age_shift", flowShift = "flow_shift"
    }
}

extension CommunityStart {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        basis = c.lenient(.basis, "none")
        setting = c.lenient(.setting, 0)
        range = c.lenient(.range, 0)
        ageShift = c.lenient(.ageShift, 0)
        flowShift = c.lenient(.flowShift, 0)
    }
}

/// The owner's own nearest coffee, as a guess to start from (core/SimilarCoffee.h).
struct CommunitySimilar: Codable, Sendable, Equatable {
    var found = false
    var setting = 0
    var range = 0
    var fromCoffee = ""
    var fromSetting = 0
    var shift = 0
    /// "fitted" (moved by the owner's own coffees) or "nearest".
    var basis = ""
    /// Coffees the fit or the range rests on.
    var samples = 0
    /// Factors that moved it: 1 roast, 2 altitude, 4 process.
    var applied = 0
    /// -1 finer, +1 coarser than the nearest, by convention; 0 none.
    var hintRoast = 0
    var hintAltitude = 0
    var hintProcess = 0
    var nearRecipe = false

    enum CodingKeys: String, CodingKey {
        case found, setting, range, shift, basis, samples, applied
        case fromCoffee = "from_coffee", fromSetting = "from_setting", hintRoast = "hint_roast"
        case hintAltitude = "hint_altitude", hintProcess = "hint_process", nearRecipe = "near_recipe"
    }
}

extension CommunitySimilar {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        found = c.lenient(.found, false)
        setting = c.lenient(.setting, 0)
        range = c.lenient(.range, 0)
        fromCoffee = c.lenient(.fromCoffee, "")
        fromSetting = c.lenient(.fromSetting, 0)
        shift = c.lenient(.shift, 0)
        basis = c.lenient(.basis, "")
        samples = c.lenient(.samples, 0)
        applied = c.lenient(.applied, 0)
        hintRoast = c.lenient(.hintRoast, 0)
        hintAltitude = c.lenient(.hintAltitude, 0)
        hintProcess = c.lenient(.hintProcess, 0)
        nearRecipe = c.lenient(.nearRecipe, false)
    }
}

/// A first guess from the grinder's range alone.
struct CommunityRangeGuess: Codable, Sendable, Equatable {
    var setting = 0
    var range = 0

    enum CodingKeys: String, CodingKey { case setting, range }
}

extension CommunityRangeGuess {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        setting = c.lenient(.setting, 0)
        range = c.lenient(.range, 0)
    }
}

/// GET /api/community?coffee=&recipe=
struct CommunityNow: Codable, Sendable, Equatable {
    /// The shared library could be reached.
    var reachable = false
    var found = false
    var contributors = 0
    var dialins = 0
    var timeS: Double = 0
    var spreadUm: Double = 0
    var sameModel = 0
    var coffeesInCommon = 0
    var myAgeDays = 0
    var theirAgeDays = 0
    var recipeMatch = ""
    var theirDoseG: Double = 0
    var theirYieldG: Double = 0
    var ageRateKnown = false
    var start = CommunityStart()
    var similar = CommunitySimilar()
    var rangeGuess: CommunityRangeGuess?

    enum CodingKeys: String, CodingKey {
        case reachable, found, contributors, dialins, start, similar
        case ageRateKnown = "age_rate_known", rangeGuess = "range_guess"
        case timeS = "time_s", spreadUm = "spread_um", sameModel = "same_model"
        case coffeesInCommon = "coffees_in_common", myAgeDays = "my_age_days", theirAgeDays = "their_age_days"
        case recipeMatch = "recipe_match", theirDoseG = "their_dose_g", theirYieldG = "their_yield_g"
    }
}

extension CommunityNow {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        reachable = c.lenient(.reachable, false)
        found = c.lenient(.found, false)
        contributors = c.lenient(.contributors, 0)
        dialins = c.lenient(.dialins, 0)
        timeS = c.lenient(.timeS, 0)
        spreadUm = c.lenient(.spreadUm, 0)
        sameModel = c.lenient(.sameModel, 0)
        coffeesInCommon = c.lenient(.coffeesInCommon, 0)
        myAgeDays = c.lenient(.myAgeDays, 0)
        theirAgeDays = c.lenient(.theirAgeDays, -1)
        recipeMatch = c.lenient(.recipeMatch, "")
        theirDoseG = c.lenient(.theirDoseG, 0)
        theirYieldG = c.lenient(.theirYieldG, 0)
        ageRateKnown = c.lenient(.ageRateKnown, false)
        start = c.lenient(.start, CommunityStart())
        similar = c.lenient(.similar, CommunitySimilar())
        rangeGuess = c.lenient(.rangeGuess)
    }
}
