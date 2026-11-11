// The words and small formats of the Subscriptions page (src/net/WebPage.h: STYLE_TXT, SRC_TXT, ASP,
// SHARE_TXT, kindTxt, platTxt, subEvery, subWhen, money), so the app reads like the board's page.
import Foundation

enum SubscriptionWording {
    static let styleKeys = ["single_roaster", "multi_roaster", "curated", "matched", "single_origin", "blends",
                            "decaf", "espresso", "filters", "cleaning", "equipment"]
    static let sourcingKeys = ["fair_trade", "direct_trade", "organic", "farm_named", "prices_published",
                               "sustainable_packaging", "carbon_neutral", "b_corp"]

    static let style: [String: String] = [
        "single_roaster": "Single roaster", "multi_roaster": "Many roasters", "curated": "Curated",
        "matched": "Matched to your taste", "single_origin": "Single origin", "blends": "Blends",
        "decaf": "Decaf", "espresso": "For espresso", "filters": "Filters", "cleaning": "Cleaning",
        "equipment": "Equipment",
    ]

    static let sourcing: [String: String] = [
        "fair_trade": "Fair trade", "direct_trade": "Direct trade", "organic": "Organic",
        "farm_named": "Farms named", "prices_published": "Prices published",
        "sustainable_packaging": "Sustainable packaging", "carbon_neutral": "Carbon neutral", "b_corp": "B Corp",
    ]

    /// The six review aspects, in the controller's order, with their words.
    static let aspects: [(key: String, label: String)] = [
        ("value", "Value"), ("quality", "Quality"), ("consistency", "Consistency"),
        ("reliability", "Reliability"), ("flexibility", "Flexibility"), ("transparency", "Transparency"),
    ]

    static let share: [String: String] = [
        "local": "On this controller only", "queued": "Waiting to be sent", "sent": "Sent: waiting for the library",
        "review": "In review", "published": "In the shared library", "declined": "Declined",
        "refused": "Refused by the library's checks",
    ]

    static let kind: [String: String] = ["coffee": "Coffee", "supplies": "Supplies", "other": "Subscription"]

    static let platform: [String: String] = [
        "recharge": "Recharge", "ordergroove": "Ordergroove", "skio": "Skio", "loop": "Loop",
        "appstle": "Appstle", "bold": "Bold", "shopify": "Shopify",
    ]

    /// "Coffee" for `kind`.
    static func kindText(_ kind: String) -> String { Self.kind[kind] ?? "" }

    /// "$18.00".
    static func money(cents: Int) -> String { "$" + BrewFormat.decimal(Double(cents) / 100, 2) }

    /// "every 4 weeks", "every month", "not regular".
    static func every(days: Int) -> String {
        if days == 0 { return "not regular" }
        if days % 7 == 0 { return days == 7 ? "every week" : "every \(days / 7) weeks" }
        if days >= 28 && days <= 31 { return "every month" }
        if days >= 58 && days <= 62 { return "every 2 months" }
        return "every \(days) days"
    }

    /// A YYYYMMDD date as a short date, "(date not known)" when 0.
    static func date(_ ymd: Int) -> String {
        guard let date = BrewFormat.date(fromYMD: ymd) else { return "(date not known)" }
        return date.formatted(date: .abbreviated, time: .omitted)
    }

    /// "Nov 20, 2026 · in 9 days", "today", "tomorrow", "... · 2 days overdue".
    static func when(_ item: SubscriptionItem) -> String {
        guard let days = item.daysUntil else { return "" }
        let text = date(item.nextDate)
        if days < 0 { return text + " · \(-days) " + (days == -1 ? "day" : "days") + " overdue" }
        if days == 0 { return "today" }
        if days == 1 { return "tomorrow" }
        return text + " · in \(days) days"
    }

    /// "1 shot a day", "2.5 shots a day".
    static func perDay(_ value: Double) -> String {
        let rounded = (value * 10).rounded() / 10
        return BrewFormat.decimal(rounded, rounded == rounded.rounded() ? 0 : 1) + (rounded == 1 ? " shot" : " shots") + " a day"
    }

    static func capitalized(_ text: String) -> String {
        guard let first = text.first else { return text }
        return first.uppercased() + text.dropFirst()
    }

    /// "every 1 / 2 / 4 wk" for a service's cadences.
    static func cadenceText(_ cadences: [Int]) -> String {
        guard !cadences.isEmpty else { return "" }
        return "every " + cadences.map { $0 % 7 != 0 ? "\($0) d" : "\($0 / 7) wk" }.joined(separator: " / ")
    }

    /// "from $12.00" (with the currency when it is not USD).
    static func priceFrom(cents: Int, currency: String) -> String {
        guard cents > 0 else { return "" }
        let symbol = currency == "USD" ? "$" : ""
        let code = (!currency.isEmpty && currency != "USD") ? " " + currency : ""
        return "from " + symbol + BrewFormat.decimal(Double(cents) / 100, 2) + code
    }

    // MARK: Dates for forms

    /// YYYYMMDD of a date (local calendar), 0 for nil.
    static func ymd(_ date: Date?) -> Int {
        guard let date else { return 0 }
        let parts = Calendar(identifier: .gregorian).dateComponents([.year, .month, .day], from: date)
        return (parts.year ?? 0) * 10_000 + (parts.month ?? 0) * 100 + (parts.day ?? 0)
    }
}
