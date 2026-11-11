// Models for the Subscriptions screen (and the subscription recommendations on Discover), lenient like
// Core/Models/LenientDecoding.swift. Field names are those of src/App.h subscriptionToJson and
// src/net/Web.cpp (snake_case), the shared library's service entries as the web page reads them.
//   GET  /api/subscriptions            SubscriptionList
//   POST /api/subscriptions            one SubscriptionItem (see `json`), {ok, id}
//   GET  /api/subscriptions/preview    SubscriptionPreview
//   GET  /api/subscriptions/reviews    SubscriptionReviewList
//   GET  /api/subscriptions/library    SubscriptionLibraryList
//   GET  /api/subscriptions/recommend  SubscriptionRecommendations
import Foundation

// MARK: - A subscription

struct SubscriptionItem: Codable, Sendable, Equatable, Identifiable {
    var id = 0
    /// "coffee", "supplies" or "other".
    var kind = "coffee"
    /// "active", "paused" or "cancelled".
    var status = "active"
    var name = ""
    var url = ""
    var manageUrl = ""
    var everyDays = 0
    /// YYYYMMDD, 0: not set.
    var nextDate = 0
    var priceCents = 0
    var lastArrived = 0
    var note = ""
    var pageTitle = ""
    var site = ""
    var pageDescription = ""
    var image = ""
    var icon = ""
    /// "#rrggbb" or "".
    var color = ""
    var platform = ""
    // Supplies
    var item = ""
    var stock = 0
    var perShot = 0
    var perDelivery = 0
    // For the shared library
    /// Country codes, comma separated.
    var shipsTo = ""
    /// Delivery intervals offered, in days.
    var cadences: [Int] = []
    var priceFromCents = 0
    var currency = ""
    var style: [String] = []
    var sourcing: [String] = []
    var libraryId = ""
    var reviewNote = ""
    // Only in the list answer
    var daysUntil: Int?
    var dueSoon = false
    var stockDaysLeft: Int?
    var stockShort = false
    /// "local", "queued", "sent", "review", "published", "declined" or "refused".
    var share = "local"
    var shareChanged = false
    var canShare = false

    enum CodingKeys: String, CodingKey {
        case id, kind, status, name, url, note, site, image, icon, color, platform, item, stock, cadences, style
        case sourcing, currency, share
        case manageUrl = "manage_url", everyDays = "every_days", nextDate = "next_date", priceCents = "price_cents"
        case lastArrived = "last_arrived", pageTitle = "title", pageDescription = "description"
        case perShot = "per_shot", perDelivery = "per_delivery", shipsTo = "ships_to"
        case priceFromCents = "price_from_cents", libraryId = "library_id", reviewNote = "review_note"
        case daysUntil = "days_until", dueSoon = "due_soon", stockDaysLeft = "stock_days_left"
        case stockShort = "stock_short", shareChanged = "share_changed", canShare = "can_share"
    }

    /// The name the card shows.
    var displayName: String {
        if !name.isEmpty { return name }
        if !pageTitle.isEmpty { return pageTitle }
        if !site.isEmpty { return site }
        return "Subscription"
    }

    var isActive: Bool { status == "active" }

    /// The body for POST /api/subscriptions (the fields `subscriptionFromJson` reads).
    var json: [String: BrewJSON] {
        var body: [String: BrewJSON] = [
            "id": .int(id), "kind": .string(kind), "status": .string(status), "name": .string(name),
            "url": .string(url), "manage_url": .string(manageUrl), "every_days": .int(everyDays),
            "next_date": .int(nextDate), "price_cents": .int(priceCents), "last_arrived": .int(lastArrived),
            "note": .string(note), "title": .string(pageTitle), "site": .string(site),
            "description": .string(pageDescription), "image": .string(image), "icon": .string(icon),
            "color": .string(color), "platform": .string(platform), "item": .string(item),
            "stock": .int(stock), "per_shot": .int(perShot), "per_delivery": .int(perDelivery),
            "ships_to": .string(shipsTo), "price_from_cents": .int(priceFromCents),
            "currency": .string(currency), "library_id": .string(libraryId), "review_note": .string(reviewNote),
        ]
        body["cadences"] = .array(cadences.map { BrewJSON.int($0) })
        body["style"] = .array(style.map { BrewJSON.string($0) })
        body["sourcing"] = .array(sourcing.map { BrewJSON.string($0) })
        return body
    }
}

extension SubscriptionItem {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        kind = c.lenient(.kind, "coffee")
        status = c.lenient(.status, "active")
        name = c.lenient(.name, "")
        url = c.lenient(.url, "")
        manageUrl = c.lenient(.manageUrl, "")
        everyDays = c.lenient(.everyDays, 0)
        nextDate = c.lenient(.nextDate, 0)
        priceCents = c.lenient(.priceCents, 0)
        lastArrived = c.lenient(.lastArrived, 0)
        note = c.lenient(.note, "")
        pageTitle = c.lenient(.pageTitle, "")
        site = c.lenient(.site, "")
        pageDescription = c.lenient(.pageDescription, "")
        image = c.lenient(.image, "")
        icon = c.lenient(.icon, "")
        color = c.lenient(.color, "")
        platform = c.lenient(.platform, "")
        item = c.lenient(.item, "")
        stock = c.lenient(.stock, 0)
        perShot = c.lenient(.perShot, 0)
        perDelivery = c.lenient(.perDelivery, 0)
        shipsTo = c.lenient(.shipsTo, "")
        cadences = c.lenient(.cadences, [])
        priceFromCents = c.lenient(.priceFromCents, 0)
        currency = c.lenient(.currency, "")
        style = c.lenient(.style, [])
        sourcing = c.lenient(.sourcing, [])
        libraryId = c.lenient(.libraryId, "")
        reviewNote = c.lenient(.reviewNote, "")
        daysUntil = c.lenient(.daysUntil)
        dueSoon = c.lenient(.dueSoon, false)
        stockDaysLeft = c.lenient(.stockDaysLeft)
        stockShort = c.lenient(.stockShort, false)
        share = c.lenient(.share, "local")
        shareChanged = c.lenient(.shareChanged, false)
        canShare = c.lenient(.canShare, false)
    }
}

struct SubscriptionList: Codable, Sendable, Equatable {
    /// YYYYMMDD on the controller.
    var today = 0
    var shotsPerDay: Double = 0
    var max = 12
    var monthlyCents = 0
    var subscriptions: [SubscriptionItem] = []
    /// The owner's country code ("" none) and what matters to them (sourcing names).
    var country = ""
    var matters: [String] = []
    /// Sharing to the library is on.
    var sharing = false
    var shareRatings = false

    enum CodingKeys: String, CodingKey {
        case today, max, subscriptions, country, matters, sharing
        case shotsPerDay = "shots_per_day", monthlyCents = "monthly_cents", shareRatings = "share_ratings"
    }

    func subscription(id: Int) -> SubscriptionItem? { subscriptions.first { $0.id == id } }
}

extension SubscriptionList {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        today = c.lenient(.today, 0)
        shotsPerDay = c.lenient(.shotsPerDay, 0)
        max = c.lenient(.max, 12)
        monthlyCents = c.lenient(.monthlyCents, 0)
        subscriptions = c.lenient(.subscriptions, [])
        country = c.lenient(.country, "")
        matters = c.lenient(.matters, [])
        sharing = c.lenient(.sharing, false)
        shareRatings = c.lenient(.shareRatings, false)
    }
}

// MARK: - Reading a shop's page

/// GET /api/subscriptions/preview: what the controller read from a page (POST {url} starts it).
struct SubscriptionPreview: Codable, Sendable, Equatable {
    /// "none", "waiting", "done" or "failed".
    var state = "none"
    var url = ""
    var problem = ""
    var title = ""
    var site = ""
    var pageDescription = ""
    var image = ""
    var icon = ""
    var color = ""
    var platform = ""
    var manageUrl = ""

    enum CodingKeys: String, CodingKey {
        case state, url, problem, title, site, image, icon, color, platform
        case pageDescription = "description", manageUrl = "manage_url"
    }
}

extension SubscriptionPreview {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        state = c.lenient(.state, "none")
        url = c.lenient(.url, "")
        problem = c.lenient(.problem, "")
        title = c.lenient(.title, "")
        site = c.lenient(.site, "")
        pageDescription = c.lenient(.pageDescription, "")
        image = c.lenient(.image, "")
        icon = c.lenient(.icon, "")
        color = c.lenient(.color, "")
        platform = c.lenient(.platform, "")
        manageUrl = c.lenient(.manageUrl, "")
    }
}

// MARK: - Reviews

/// One drinker's review of a subscription.
struct SubscriptionReview: Codable, Sendable, Equatable, Identifiable {
    var id = 0
    var subscription = 0
    /// 0 the household, 1...8 a named drinker.
    var drinker = 0
    var stars = 0
    var at = 0
    /// Aspect name (value, quality, ...) to 1...5; an aspect not given is missing.
    var aspects: [String: Int] = [:]
    var note = ""
    var share = "local"

    enum CodingKeys: String, CodingKey { case id, subscription, drinker, stars, at, aspects, note, share }
}

extension SubscriptionReview {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        subscription = c.lenient(.subscription, 0)
        drinker = c.lenient(.drinker, 0)
        stars = c.lenient(.stars, 0)
        at = c.lenient(.at, 0)
        aspects = c.lenient(.aspects, [:])
        note = c.lenient(.note, "")
        share = c.lenient(.share, "local")
    }
}

struct SubscriptionReviewList: Codable, Sendable, Equatable {
    var reviews: [SubscriptionReview] = []

    enum CodingKeys: String, CodingKey { case reviews }
}

extension SubscriptionReviewList {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        reviews = c.lenient(.reviews, [])
    }
}

// MARK: - The shared library's services

struct SubscriptionLibraryReviews: Codable, Sendable, Equatable {
    var stars: Double = 0
    var n = 0

    enum CodingKeys: String, CodingKey { case stars, n }
}

extension SubscriptionLibraryReviews {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        stars = c.lenient(.stars, 0)
        n = c.lenient(.n, 0)
    }
}

/// A service listed in the shared library (docs/SUBSCRIPTIONS.md).
struct SubscriptionLibraryService: Codable, Sendable, Equatable, Identifiable {
    var id = ""
    var name = ""
    var url = ""
    var manageUrl = ""
    var kind = "coffee"
    var platform = ""
    var image = ""
    var icon = ""
    var color = ""
    var about = ""
    /// Delivery intervals offered, in days.
    var cadences: [Int] = []
    /// Country codes it ships to.
    var shipsTo: [String] = []
    var priceFromCents = 0
    var currency = ""
    var style: [String] = []
    var sourcing: [String] = []
    var reviews: SubscriptionLibraryReviews?

    enum CodingKeys: String, CodingKey {
        case id, name, url, kind, platform, image, icon, color, about, cadences, currency, style, sourcing, reviews
        case manageUrl = "manage_url", shipsTo = "ships_to", priceFromCents = "price_from_cents"
    }

    /// The body to add it to the owner's subscriptions (web page libAdd).
    var addJSON: [String: BrewJSON] {
        [
            "id": .int(0), "library_id": .string(id), "name": .string(name), "url": .string(url),
            "manage_url": .string(manageUrl), "kind": .string(kind), "status": .string("active"),
            "platform": .string(platform), "image": .string(image), "icon": .string(icon),
            "color": .string(color), "every_days": .int(cadences.first ?? 0),
            "ships_to": .string(shipsTo.joined(separator: ",")),
            "cadences": .array(cadences.map { BrewJSON.int($0) }), "price_from_cents": .int(priceFromCents),
            "currency": .string(currency), "style": .array(style.map { BrewJSON.string($0) }),
            "sourcing": .array(sourcing.map { BrewJSON.string($0) }), "description": .string(about),
        ]
    }
}

extension SubscriptionLibraryService {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, "")
        name = c.lenient(.name, "")
        url = c.lenient(.url, "")
        manageUrl = c.lenient(.manageUrl, "")
        kind = c.lenient(.kind, "coffee")
        platform = c.lenient(.platform, "")
        image = c.lenient(.image, "")
        icon = c.lenient(.icon, "")
        color = c.lenient(.color, "")
        about = c.lenient(.about, "")
        cadences = c.lenient(.cadences, [])
        shipsTo = c.lenient(.shipsTo, [])
        priceFromCents = c.lenient(.priceFromCents, 0)
        currency = c.lenient(.currency, "")
        style = c.lenient(.style, [])
        sourcing = c.lenient(.sourcing, [])
        reviews = c.lenient(.reviews)
    }
}

struct SubscriptionLibraryList: Codable, Sendable, Equatable {
    var services: [SubscriptionLibraryService] = []

    enum CodingKeys: String, CodingKey { case services }
}

extension SubscriptionLibraryList {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        services = c.lenient(.services, [])
    }
}

// MARK: - Recommendations

struct SubscriptionRecommendation: Codable, Sendable, Equatable, Identifiable {
    var service = SubscriptionLibraryService()
    /// Short reasons ("matches your taste").
    var why: [String] = []
    /// A line about people with similar taste; "" none.
    var people = ""
    var id: String { service.id }

    enum CodingKeys: String, CodingKey { case service, why, people }
}

extension SubscriptionRecommendation {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        service = c.lenient(.service, SubscriptionLibraryService())
        why = c.lenient(.why, [])
        people = c.lenient(.people, "")
    }
}

/// GET /api/subscriptions/recommend?drinker=
struct SubscriptionRecommendations: Codable, Sendable, Equatable {
    /// "taste", "reviews" or "none".
    var basis = "none"
    var shown = false
    /// Coffees rated so far.
    var coffees = 0
    var country = ""
    var matters: [String] = []
    var items: [SubscriptionRecommendation] = []

    enum CodingKeys: String, CodingKey { case basis, shown, coffees, country, matters, items }
}

extension SubscriptionRecommendations {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        basis = c.lenient(.basis, "none")
        shown = c.lenient(.shown, false)
        coffees = c.lenient(.coffees, 0)
        country = c.lenient(.country, "")
        matters = c.lenient(.matters, [])
        items = c.lenient(.items, [])
    }
}
