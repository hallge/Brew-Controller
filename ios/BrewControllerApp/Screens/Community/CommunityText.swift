// The words of the Community page and the Reddit post, ported from the web page (src/net/WebPage.h:
// LK_KINDS, LK_TOPICS, LK_STATE, METHOD, ytId, lkThumb, rdBuild). Pure functions, so the unit tests can
// check them.
import Foundation

enum CommunityText {
    static let kinds: [(key: String, label: String)] = [
        ("video", "Video"), ("playlist", "Playlist"), ("post", "Post"), ("guide", "Guide"),
        ("article", "Article"), ("podcast", "Podcast"),
    ]

    static let topics: [(key: String, label: String)] = [
        ("preparation", "Preparation"), ("dialing_in", "Dialling in"), ("brewing", "Brewing"), ("beans", "Beans"),
        ("gear", "Gear"), ("milk", "Milk"), ("maintenance", "Upkeep"), ("science", "Science"),
    ]

    /// Where a suggestion of yours stands.
    static let states: [String: String] = [
        "queued": "waiting to send", "sent": "sent", "review": "with a reviewer",
        "published": "approved: in the feed", "declined": "declined",
        "refused": "refused by the library's checks", "local": "not sent",
    ]

    /// How a trending recipe is pulled.
    static let methods: [String: String] = [
        "straight": "straight through", "preinfusion": "held pre-infusion", "bloom": "bloom",
    ]

    static func kindLabel(_ key: String) -> String { kinds.first { $0.key == key }?.label ?? key }
    static func topicLabel(_ key: String) -> String { topics.first { $0.key == key }?.label ?? key }

    // MARK: Thumbnails

    /// The 11-character id of a YouTube watch, short or shorts link; nil for any other link.
    static func youtubeID(_ url: String) -> String? {
        let pattern = #"(?:youtube\.com/watch\?(?:.*&)?v=|youtu\.be/|youtube\.com/shorts/)([\w-]{11})"#
        guard let expression = try? NSRegularExpression(pattern: pattern) else { return nil }
        let range = NSRange(url.startIndex..., in: url)
        guard let match = expression.firstMatch(in: url, range: range), match.numberOfRanges > 1,
              let id = Range(match.range(at: 1), in: url) else { return nil }
        return String(url[id])
    }

    /// The picture of a feed card: the link's own, or the YouTube thumbnail.
    static func thumbnail(image: String, url: String) -> URL? {
        if !image.isEmpty { return URL(string: image) }
        if let id = youtubeID(url) { return URL(string: "https://i.ytimg.com/vi/\(id)/hqdefault.jpg") }
        return nil
    }

    // MARK: Trending

    /// "1:2.0 in about 28 s, held pre-infusion".
    static func trendingLine(_ recipe: CommunityTrendingRecipe) -> String {
        "1:" + BrewFormat.decimal(recipe.ratio, 1) + " in about " + BrewFormat.decimal(recipe.timeS, 0) + " s, "
            + (methods[recipe.method] ?? recipe.method)
    }

    /// "5 people, 3 dial-ins, 2 coffees".
    static func trendingCounts(_ recipe: CommunityTrendingRecipe) -> String {
        "\(recipe.contributors) people, \(recipe.dialins) dial-in\(recipe.dialins == 1 ? "" : "s"), "
            + "\(recipe.coffees) coffee\(recipe.coffees == 1 ? "" : "s")"
    }

    // MARK: "For what you are brewing"

    /// Why the start setting is what it is (the web page's `why` text).
    static func startWhy(_ now: CommunityNow) -> String {
        let start = now.start
        var text: String
        switch start.basis {
        case "fitted":
            text = "translated to your grinder through \(now.coffeesInCommon) coffee\(now.coffeesInCommon == 1 ? "" : "s") you have in common with others"
        case "same_model":
            text = "what \(now.sameModel) other\(now.sameModel == 1 ? "" : "s") with your grinder model set"
        case "model":
            text = "translated to your grinder through what the library has learned from other owners of your grinder model"
        case "microns":
            text = "the shared scale read as microns; how your own grinder sits against it is not known yet"
        default:
            text = "cannot be translated to your grinder yet"
        }
        if now.recipeMatch == "near" {
            text += "; nobody has settled this exact recipe, so this comes from \(BrewFormat.decimal(now.theirDoseG, 1)) g in, \(BrewFormat.decimal(now.theirYieldG, 1)) g out, with a wider range"
        }
        return text
    }

    // MARK: The Reddit post

    static let subreddits: [(key: String, label: String)] = [
        ("espresso", "r/espresso"), ("Coffee", "r/Coffee"), ("BrevilleCoffee", "r/BrevilleCoffee"),
    ]

    static let footerLink = "github.com/hallge/Brew-Controller"

    /// What the shot's picture and text are made from.
    struct RedditShot {
        var shot: ShotModel
        var coffee: CoffeeModel?
        var grinder: GrinderModel

        var name: String { coffee?.name ?? (shot.coffee.isEmpty ? "Espresso" : shot.coffee) }
        var dose: Double { shot.doseG > 0 ? shot.doseG : 0 }
        var out: Double { shot.finalWeightG }
        var ratio: String { dose > 0 ? BrewFormat.decimal(out / dose, 1) : "" }

        /// Days from the roast to the shot; nil when either date is not known.
        var daysOffRoast: Int? {
            guard let roast = BrewFormat.date(fromYMD: coffee?.roastDate), let when = shot.date else { return nil }
            let days = Int((when.timeIntervalSince(roast) / 86_400).rounded())
            return days >= 0 ? days : nil
        }

        var grindText: String { shot.grindUm > 0 ? grinder.dial.settingUnit(shot.grindUm) : "not recorded" }

        var startText: String {
            if shot.bloomPauseS > 0 { return "bloom \(BrewFormat.decimal(shot.bloomPauseS, 0)) s" }
            if shot.preinfusionHoldS > 0 { return "held pre-infusion" }
            return "machine pre-infusion"
        }

        var roastedText: String {
            guard let coffee, coffee.roastDate != 0 else { return "" }
            return SubscriptionWording.date(coffee.roastDate) + (daysOffRoast.map { " (\($0) days)" } ?? "")
        }
    }

    /// "Guji Natural (Demo Roasters): 18.0 g in, 37.2 g out in 26.1 s".
    static func redditTitle(_ item: RedditShot) -> String {
        var title = item.name
        if let roaster = item.coffee?.roaster, !roaster.isEmpty { title += " (\(roaster))" }
        title += ": "
        if item.dose > 0 { title += BrewFormat.decimal(item.dose, 1) + " g in, " }
        title += BrewFormat.decimal(item.out, 1) + " g out in " + BrewFormat.decimal(item.shot.shotTimeS, 1) + " s"
        return title
    }

    /// The post's text, in Reddit's Markdown.
    static func redditText(_ item: RedditShot) -> String {
        let shot = item.shot
        var lines: [String] = []

        var coffee = "**Coffee:** " + item.name
        if let c = item.coffee {
            if !c.roaster.isEmpty { coffee += " from " + c.roaster }
            let parts = [c.origin, c.process].filter { !$0.isEmpty }
            if !parts.isEmpty { coffee += " (" + parts.joined(separator: ", ") + ")" }
            if c.roastDate != 0 {
                coffee += ", roasted " + SubscriptionWording.date(c.roastDate)
                if let days = item.daysOffRoast { coffee += " (\(days) days before this shot)" }
            }
        }
        lines.append(coffee)

        var recipe = "**Recipe:** "
        if item.dose > 0 { recipe += BrewFormat.decimal(item.dose, 1) + " g in, " }
        recipe += BrewFormat.decimal(item.out, 1) + " g out"
        if !item.ratio.isEmpty { recipe += " (1:\(item.ratio))" }
        recipe += " in " + BrewFormat.decimal(shot.shotTimeS, 1) + " s"
        if shot.firstDropS > 0 { recipe += "; first drops at " + BrewFormat.decimal(shot.firstDropS, 1) + " s" }
        if shot.bloomPauseS > 0 { recipe += "; bloom \(BrewFormat.decimal(shot.bloomPauseS, 0)) s" }
        lines.append(recipe)

        lines.append("**Grind:** " + item.grindText + (item.grinder.model.isEmpty ? "" : " on a " + item.grinder.model))
        lines.append("**Flow:** " + BrewFormat.decimal(shot.pourFlowGS, 2) + " g/s on average while pouring, peak "
                     + BrewFormat.decimal(shot.peakFlowGS, 2) + " g/s")
        if !shot.taste.isEmpty {
            lines.append("**Taste:** " + shot.taste + (shot.rating > 0 ? ", \(shot.rating)/5" : ""))
        }
        if let notes = item.coffee?.notes, !notes.isEmpty { lines.append("**Tasting notes on the bag:** " + notes) }
        lines.append("")
        lines.append("*Stopped by weight with an open-source brew-by-weight controller: https://" + footerLink + "*")
        return lines.joined(separator: "\n\n").replacingOccurrences(of: "\n\n\n\n", with: "\n\n")
    }

    /// The rows under the curve on the picture: label and value, empty values left out.
    static func redditRows(_ item: RedditShot) -> [(label: String, value: String)] {
        let shot = item.shot
        let rows: [(String, String)] = [
            ("Dose", item.dose > 0 ? BrewFormat.decimal(item.dose, 1) + " g" : "not set"),
            ("Yield", BrewFormat.decimal(item.out, 1) + " g" + (item.ratio.isEmpty ? "" : " (1:\(item.ratio))")),
            ("Time", BrewFormat.decimal(shot.shotTimeS, 1) + " s"
                + (shot.firstDropS > 0 ? ", first drops " + BrewFormat.decimal(shot.firstDropS, 1) + " s" : "")),
            ("Flow", BrewFormat.decimal(shot.pourFlowGS, 2) + " g/s, peak " + BrewFormat.decimal(shot.peakFlowGS, 2)),
            ("Grind", item.grindText + (item.grinder.model.isEmpty ? "" : " on " + item.grinder.model)),
            ("Start", item.startText),
            ("Roasted", item.roastedText),
        ]
        return rows.filter { !$0.1.isEmpty }.map { (label: $0.0, value: $0.1) }
    }
}
