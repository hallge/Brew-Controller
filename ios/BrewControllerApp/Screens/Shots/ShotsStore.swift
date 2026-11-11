// The Shots screens' state: the shot history (up to 500, as the web page reads it), the filters and the
// summary tiles, the comparison, and every change the detail screen makes (vote, grind, rating, TDS,
// preparation). The words and the arithmetic follow WebPage.h `drawShots` and `showShot`.
import Foundation
import Observation

// MARK: - Filters and the words of a shot

/// The History "Show" menu.
enum ShotsShow: Int, CaseIterable, Identifiable, Sendable {
    case all = 0, byWeight, offTarget, notRated

    var id: Int { rawValue }

    var label: String {
        switch self {
        case .all: return "All shots"
        case .byWeight: return "Stopped by weight"
        case .offTarget: return "Brew time off target"
        case .notRated: return "Not rated yet"
        }
    }
}

/// One tile of the summary row.
struct ShotsSummaryTile: Identifiable, Equatable, Sendable {
    var id: String { title }
    var title: String
    var value: String
    var detail: String
}

enum ShotsText {
    /// The controller's stop target when it had one, else the recipe's.
    static func stopTarget(_ shot: ShotModel) -> Double {
        shot.stopTargetG > 0 ? shot.stopTargetG : shot.targetWeightG
    }

    /// Stopped by the weight: "weight" or "weight_predicted".
    static func byWeight(_ shot: ShotModel) -> Bool { shot.endReason.hasPrefix("weight") }

    /// "weight predicted" for "weight_predicted".
    static func endReason(_ shot: ShotModel) -> String {
        shot.endReason.replacingOccurrences(of: "_", with: " ")
    }

    /// The brew time the target time is compared with: the pour time on a "pour" basis.
    static func brewTime(_ shot: ShotModel) -> Double {
        shot.timeBasis == "pour" ? shot.pourTimeS : shot.shotTimeS
    }

    /// Stopped by weight and the brew time is outside the tolerance of the target time.
    static func late(_ shot: ShotModel, tolerance: Double) -> Bool {
        byWeight(shot) && shot.targetTimeS > 0 && abs(brewTime(shot) - shot.targetTimeS) > tolerance
    }

    /// "Today", "Yesterday", "Mon, 9 Nov", or "Date not known" when the clock was not set.
    static func day(_ shot: ShotModel, now: Date = Date()) -> String {
        guard let date = shot.date else { return "Date not known" }
        let calendar = Calendar.current
        if calendar.isDate(date, inSameDayAs: now) { return "Today" }
        if let yesterday = calendar.date(byAdding: .day, value: -1, to: now), calendar.isDate(date, inSameDayAs: yesterday) {
            return "Yesterday"
        }
        let sameYear = calendar.component(.year, from: date) == calendar.component(.year, from: now)
        if sameYear { return date.formatted(.dateTime.weekday(.abbreviated).day().month(.abbreviated)) }
        return date.formatted(.dateTime.weekday(.abbreviated).day().month(.abbreviated).year())
    }

    /// "9:05 AM"; "#12" when the clock was not set.
    static func clock(_ shot: ShotModel) -> String {
        guard let date = shot.date else { return "#\(shot.id)" }
        return date.formatted(date: .omitted, time: .shortened)
    }

    /// "9 Nov, 9:05 AM"; "#12" when the clock was not set.
    static func when(_ shot: ShotModel) -> String {
        guard let date = shot.date else { return "#\(shot.id)" }
        return date.formatted(.dateTime.month(.abbreviated).day().hour().minute())
    }

    static func stars(_ count: Int) -> String { String(repeating: "★", count: max(0, min(5, count))) }

    /// The index the rating route uses for a taste word (0 none, 1 sour, 2 balanced, 3 bitter).
    static let tastes = ["", "sour", "balanced", "bitter"]
}

/// Grind settings as text: the dial's own way of writing them (core/SettingFormat.h), read back.
enum ShotsGrind {
    /// "246", "3.5" or "12:3" as stored steps; nil when the text is not a setting this dial can have.
    static func parse(_ text: String, dial: BrewFormat.Dial) -> Int? {
        let trimmed = text.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty else { return nil }
        let steps: Int
        if dial.isRinged {
            let parts = trimmed.split(whereSeparator: { ":/-".contains($0) }).map { $0.trimmingCharacters(in: .whitespaces) }
            guard parts.count == 1 || parts.count == 2, let outer = Int(parts[0]) else { return nil }
            let inner = parts.count == 2 ? Int(parts[1]) : 1
            guard let inner, outer >= 1, inner >= 1, inner <= dial.subSteps else { return nil }
            steps = (outer - 1) * dial.subSteps + inner
        } else {
            let normal = trimmed.replacingOccurrences(of: ",", with: ".")
            guard normal.allSatisfy({ $0.isNumber || $0 == "." }), let value = Double(normal), value.isFinite else { return nil }
            steps = Int((value * Double(dial.divisor) + 0.5).rounded(.down))
        }
        return steps >= 1 && steps <= 2000 ? steps : nil
    }
}

// MARK: - Store

@MainActor
@Observable
final class ShotsStore {
    private(set) var shots: [ShotModel] = []
    private(set) var library: LibraryModel?
    /// How far a brew time may be from the target before it counts as off (Setup, "grind tolerance").
    private(set) var toleranceS: Double = 2
    private(set) var isLoading = false
    private(set) var loadedOnce = false
    /// A shot is running: the controller answers 409 and the history waits for it.
    private(set) var busy = false
    var errorText: String?
    private(set) var notice: String?

    /// Shots chosen for the comparison, in the order they were added (up to 8).
    private(set) var compareIDs: [Int] = []

    // Filters
    var coffeeFilter: String = ""
    var show: ShotsShow = .all
    /// How many rows the list shows ("Show 20 more").
    var shownCount = 20

    @ObservationIgnored private let api: BrewAPI
    @ObservationIgnored private var curves: [Int: ShotDataModel] = [:]
    @ObservationIgnored private var noticeTask: Task<Void, Never>?

    static let maxCompare = 8

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    var dial: BrewFormat.Dial { library?.grinder.dial ?? .microns }

    // MARK: Derived

    /// The coffee names in the history, for the filter menu.
    var coffeeNames: [String] {
        var seen = Set<String>()
        return shots.compactMap { shot in
            guard !shot.coffee.isEmpty, seen.insert(shot.coffee).inserted else { return nil }
            return shot.coffee
        }
    }

    var filtered: [ShotModel] {
        shots.filter { shot in
            guard coffeeFilter.isEmpty || shot.coffee == coffeeFilter else { return false }
            switch show {
            case .all: return true
            case .byWeight: return ShotsText.byWeight(shot)
            case .offTarget: return ShotsText.late(shot, tolerance: toleranceS)
            case .notRated: return shot.rating == 0 && shot.taste.isEmpty
            }
        }
    }

    /// Shots, on time %, typical miss and rated average, over the filtered shots.
    var summary: [ShotsSummaryTile] {
        let all = filtered
        let weighed = all.filter(ShotsText.byWeight)
        let timed = weighed.filter { $0.targetTimeS > 0 }
        let good = timed.filter { !ShotsText.late($0, tolerance: toleranceS) }.count
        let misses = weighed.map { abs($0.finalWeightG - ShotsText.stopTarget($0)) }.sorted()
        let rated = all.filter { $0.rating > 0 }
        let average = rated.isEmpty ? 0 : Double(rated.reduce(0) { $0 + $1.rating }) / Double(rated.count)
        return [
            ShotsSummaryTile(title: "Shots", value: String(all.count), detail: "\(weighed.count) stopped by weight"),
            ShotsSummaryTile(title: "On time",
                             value: timed.isEmpty ? "--" : "\(Int((100 * Double(good) / Double(timed.count)).rounded())) %",
                             detail: timed.isEmpty ? "no target times yet"
                                 : "\(good) of \(timed.count) within ± \(BrewFormat.decimal(toleranceS, 0)) s"),
            ShotsSummaryTile(title: "Typical miss",
                             value: misses.isEmpty ? "--" : BrewFormat.grams(misses[misses.count / 2]) + " g",
                             detail: "from the target weight"),
            ShotsSummaryTile(title: "Rated",
                             value: rated.isEmpty ? "--" : BrewFormat.decimal(average, 1) + " ★",
                             detail: "\(rated.count) of \(all.count) shots"),
        ]
    }

    func shot(id: Int) -> ShotModel? { shots.first { $0.id == id } }

    /// The best-rated shot (4 stars or more) of the same coffee and recipe that has a curve, as the web
    /// page draws it dashed behind the shot.
    func referenceShot(for shot: ShotModel) -> ShotModel? {
        shots.filter { $0.id != shot.id && $0.coffeeId == shot.coffeeId && $0.recipeId == shot.recipeId
            && $0.hasData && $0.rating >= 4 }
            .sorted { ($0.rating, $0.id) > ($1.rating, $1.id) }
            .first
    }

    // MARK: Loading

    /// Reads the history and the grinder and tolerance it is shown with.
    func load() async {
        isLoading = true
        defer { isLoading = false }
        var attempt = 0
        while true {
            attempt += 1
            do {
                shots = try await api.shots(limit: 500)
                busy = false
                errorText = nil
                loadedOnce = true
                break
            } catch let error as BrewAPIError {
                if error.isConflict {
                    busy = true
                    return
                }
                if attempt < 5, RetryingTask<Int>.isRetryable(error) {
                    try? await Task.sleep(for: .milliseconds(500))
                    continue
                }
                if shots.isEmpty { errorText = error.message }
                return
            } catch {
                return
            }
        }
        if let library = try? await api.library() { self.library = library }
        if let settings = try? await api.settings(), settings.grindToleranceS > 0 { toleranceS = settings.grindToleranceS }
    }

    /// The curve of one shot, kept while the screen is open.
    func curve(id: Int) async -> ShotDataModel? {
        if let cached = curves[id] { return cached }
        guard let data = try? await api.shotData(id: id), !data.timeS.isEmpty else { return nil }
        curves[id] = data
        return data
    }

    // MARK: Comparison

    func isComparing(_ id: Int) -> Bool { compareIDs.contains(id) }

    func toggleCompare(_ id: Int) {
        if let index = compareIDs.firstIndex(of: id) {
            compareIDs.remove(at: index)
        } else if compareIDs.count >= Self.maxCompare {
            errorText = "Up to 8 shots can be compared"
        } else {
            compareIDs.append(id)
        }
    }

    func clearCompare() { compareIDs = [] }

    // MARK: Changes to one shot

    /// 0 auto, 1 ignore, 2 count (the route's numbers).
    func vote(_ id: Int, value: Int) async {
        await change("/api/shot/vote", ["id": String(id), "vote": String(value)])
    }

    func setGrind(_ id: Int, steps: Int) async {
        await change("/api/shot/grind", ["id": String(id), "um": String(steps)])
    }

    /// `rating` 0...5 (0 clears), `taste` 0...3 (0 clears).
    func rate(_ id: Int, rating: Int, taste: Int) async {
        await change("/api/shot/rate", ["id": String(id), "rating": String(rating), "taste": String(taste)])
    }

    /// `tds` in percent, 0 clears.
    func setTDS(_ id: Int, tds: Double) async {
        await change("/api/shot/tds", ["id": String(id), "tds": BrewFormat.decimal(tds, 2)])
    }

    /// Dose weighed (0: not weighed), purged first, flow looked uneven. Marking a shot uneven also sets
    /// its grind vote to Ignore, unless the owner has already chosen one.
    func setPrep(_ id: Int, dose: Double, purged: Bool, uneven: Bool) async {
        let before = shot(id)
        do {
            try await api.post("/api/shot/prep", query: [
                "id": String(id), "dose": BrewFormat.decimal(dose, 1),
                "purged": purged ? "1" : "0", "uneven": uneven ? "1" : "0",
            ])
            if uneven, let before, before.grindVote.isEmpty, !before.uneven {
                try await api.post("/api/shot/vote", query: ["id": String(id), "vote": "1"])
                announce("Saved. This uneven shot is set to Ignore for the grind advice; choose Count to use it anyway.")
            } else if uneven {
                announce("Saved. If this uneven shot should not steer the grind, choose Ignore.")
            } else {
                announce("Saved")
            }
            await load()
        } catch {
            fail(error)
        }
    }

    func clearHistory() async {
        do {
            try await api.post("/api/history/clear")
            curves = [:]
            compareIDs = []
            await load()
            announce("History cleared")
        } catch {
            fail(error)
        }
    }

    // MARK: Files

    func downloadCSV() async -> BrewDownload? {
        await download { try await self.api.downloadShotsCSV() }
    }

    func downloadJSON() async -> BrewDownload? {
        await download { try await self.api.download("/api/shots", query: ["limit": "500"], fallbackName: "shots.json") }
    }

    func downloadShotCSV(id: Int) async -> BrewDownload? {
        await download { try await self.api.downloadShotCSV(id: UInt32(id)) }
    }

    private func download(_ work: () async throws -> BrewDownload) async -> BrewDownload? {
        do {
            return try await work()
        } catch {
            fail(error)
            return nil
        }
    }

    // MARK: Helpers

    private func change(_ path: String, _ query: [String: String]) async {
        do {
            try await api.post(path, query: query)
            await load()
        } catch {
            fail(error)
        }
    }

    func fail(_ error: Error) {
        errorText = (error as? BrewAPIError)?.message ?? error.localizedDescription
    }

    func announce(_ text: String) {
        notice = text
        noticeTask?.cancel()
        noticeTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(4))
            if !Task.isCancelled { self?.notice = nil }
        }
    }
}
