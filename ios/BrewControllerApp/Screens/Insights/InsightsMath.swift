// The sums behind the Insights charts, ported from the web page (src/net/WebPage.h: insShots, drawRoastGrind,
// drawAgeHeat, drawCalendar, drawDialIn, drawGrindAcc, drawTaste). Pure functions on the models, so the
// unit tests can check them without a screen.
import Foundation

enum InsightsMath {
    // MARK: Shots

    /// The roast level names in the controller's order (index 0: not given).
    static let roastLevels = ["", "light", "medium-light", "medium", "medium-dark", "dark"]

    static func roastIndex(_ name: String) -> Int { roastLevels.firstIndex(of: name) ?? 0 }

    /// A shot stopped by weight (`weight`, `weight_predicted`, ...).
    static func byWeight(_ shot: ShotModel) -> Bool { shot.endReason.hasPrefix("weight") }

    /// The brew time the shot is judged by: the pour time when its recipe times the pour, else the total.
    static func brewTime(_ shot: ShotModel) -> Double { shot.timeBasis == "pour" ? shot.pourTimeS : shot.shotTimeS }

    /// Days from the roast date to the shot; nil when either is not known or the age is not 0...999.
    static func ageDays(_ shot: ShotModel) -> Int? {
        guard let roast = BrewFormat.date(fromYMD: shot.roastDate), let when = shot.date else { return nil }
        let days = Int((when.timeIntervalSince(roast) / 86_400).rounded(.down))
        return days >= 0 && days < 1000 ? days : nil
    }

    /// The grind tolerance the controller uses (seconds), 2 when the setting is not known.
    static func tolerance(_ settings: SettingsModel?) -> Double {
        let value = settings?.grindToleranceS ?? 0
        return value > 0 ? value : 2
    }

    /// A stored grinder setting as the dial's number (whole steps, or the fraction a dial with a
    /// divisor reads), for a chart axis.
    static func axisValue(_ stored: Int, dial: BrewFormat.Dial) -> Double {
        if dial.isRinged || dial.divisor <= 1 { return Double(stored) }
        return Double(stored) / Double(dial.divisor)
    }

    /// How a shot ran against its target time.
    enum Pace: String, Sendable { case onTime, fast, slow }

    static func pace(error: Double, tolerance: Double) -> Pace {
        if abs(error) <= tolerance { return .onTime }
        return error < 0 ? .fast : .slow
    }

    // MARK: Roast level against grind setting

    struct RoastPoint: Identifiable, Equatable {
        var id: Int
        var level: Int
        var setting: Double
        var ageDays: Int?
        var flowGS: Double
        var timeS: Double
        var targetS: Double
        var coffee: String
    }

    /// One dot per shot stopped by weight on the grinder in use, with a grind setting and a roast level.
    static func roastGrindPoints(shots: [ShotModel], calibration: Int, dial: BrewFormat.Dial) -> [RoastPoint] {
        shots.filter { shot in
            byWeight(shot) && shot.grindUm > 0 && roastIndex(shot.roastLevel) > 0
                && shot.grinderCalibration == calibration
        }.map { shot in
            RoastPoint(id: shot.id, level: roastIndex(shot.roastLevel),
                       setting: axisValue(shot.grindUm, dial: dial), ageDays: ageDays(shot),
                       flowGS: shot.pourFlowGS, timeS: brewTime(shot), targetS: shot.targetTimeS,
                       coffee: shot.coffee)
        }
    }

    struct OnTimeBand: Identifiable, Equatable {
        var level: Int
        var low: Double
        var high: Double
        var middle: Double
        var count: Int
        var id: Int { level }
    }

    /// On each roast level, the settings whose shots ran on time: lowest to highest (10th to 90th
    /// percentile from five shots on) and the middle one.
    static func onTimeBands(points: [RoastPoint], tolerance: Double) -> [OnTimeBand] {
        let levels = Set(points.map { $0.level }).sorted()
        var bands: [OnTimeBand] = []
        for level in levels {
            let settings = points.filter { point in
                point.level == level && point.targetS > 0 && point.timeS > 0
                    && abs(point.timeS - point.targetS) <= tolerance
            }.map { $0.setting }.sorted()
            guard !settings.isEmpty else { continue }
            func quantile(_ fraction: Double) -> Double {
                let index = Int((fraction * Double(settings.count - 1)).rounded())
                return settings[min(settings.count - 1, max(0, index))]
            }
            let low = settings.count >= 5 ? quantile(0.1) : settings[0]
            let high = settings.count >= 5 ? quantile(0.9) : settings[settings.count - 1]
            bands.append(OnTimeBand(level: level, low: low, high: high, middle: quantile(0.5), count: settings.count))
        }
        return bands
    }

    // MARK: Coffee age against brew time

    /// The brew-time-from-target rows, two seconds each; the outer rows are open ended.
    static let timeEdges: [Double] = [-.infinity, -6, -4, -2, 0, 2, 4, 6, .infinity]

    /// The words for one row: "≤ −6 s", "-2 to 0 s", "≥ +6 s".
    static func rowLabel(_ row: Int) -> String {
        let last = timeEdges.count - 2
        func edge(_ value: Double) -> String { (value > 0 ? "+" : "") + String(Int(value)) }
        if row == 0 { return "≤ −6 s" }
        if row == last { return "≥ +6 s" }
        return edge(timeEdges[row]) + " to " + edge(timeEdges[row + 1]) + " s"
    }

    struct AgeHeat: Equatable {
        /// counts[week][row]
        var counts: [[Int]]
        var maxCount: Int
        var total: Int
        /// Shots stopped by weight that had no roast date or no target time.
        var leftOut: Int
        var weeks: Int { counts.count }
        var rows: Int { InsightsMath.timeEdges.count - 1 }
    }

    /// Days off roast (a column a week, at most 26) against brew time minus target.
    static func ageHeat(shots: [ShotModel]) -> AgeHeat? {
        let all = shots.filter(byWeight)
        let used = all.filter { $0.targetTimeS > 0 && ageDays($0) != nil && brewTime($0) > 0 }
        guard !used.isEmpty else { return nil }
        let oldest = used.compactMap { ageDays($0) }.max() ?? 0
        let weeks = min(26, oldest / 7 + 1)
        let rows = timeEdges.count - 1
        var counts = [[Int]](repeating: [Int](repeating: 0, count: rows), count: weeks)
        for shot in used {
            let week = min(weeks - 1, (ageDays(shot) ?? 0) / 7)
            let error = brewTime(shot) - shot.targetTimeS
            var row = 0
            while row < rows - 1 && error >= timeEdges[row + 1] { row += 1 }
            counts[week][row] += 1
        }
        let top = counts.flatMap { $0 }.max() ?? 1
        return AgeHeat(counts: counts, maxCount: max(1, top), total: used.count, leftOut: all.count - used.count)
    }

    // MARK: Brewing calendar

    enum CalendarMode: String, CaseIterable, Identifiable, Sendable {
        case shots, brew, drift, hours
        var id: String { rawValue }
        /// The button words of the web page.
        var title: String {
            switch self {
            case .shots: return "Shots"
            case .brew: return "Coffee & recipe"
            case .drift: return "Drift"
            case .hours: return "Time of day"
            }
        }
    }

    struct CalendarDay: Identifiable, Equatable {
        var date: Date
        /// 0...25, oldest first.
        var week: Int
        /// 0 Monday ... 6 Sunday.
        var weekday: Int
        var shots: [ShotModel]
        var id: Date { date }
        var count: Int { shots.count }

        /// Average brew time minus target over the shots timed against a target; nil when none were.
        var drift: Double? {
            let timed = shots.filter { shot in byWeight(shot) && shot.targetTimeS > 0 && brewTime(shot) > 0 }
            guard !timed.isEmpty else { return nil }
            return timed.map { brewTime($0) - $0.targetTimeS }.reduce(0, +) / Double(timed.count)
        }
    }

    /// Monday-first weekday number of a date: 0 Monday ... 6 Sunday.
    static func weekdayIndex(_ date: Date, calendar: Calendar) -> Int {
        (calendar.component(.weekday, from: date) + 5) % 7
    }

    /// The last 26 weeks, a column a week, every day up to today.
    static func calendarDays(shots: [ShotModel], now: Date = Date(), calendar: Calendar = .current) -> [CalendarDay] {
        let today = calendar.startOfDay(for: now)
        let mondayOffset = weekdayIndex(today, calendar: calendar)
        guard let thisMonday = calendar.date(byAdding: .day, value: -mondayOffset, to: today),
              let start = calendar.date(byAdding: .day, value: -25 * 7, to: thisMonday) else { return [] }
        var byDay: [Date: [ShotModel]] = [:]
        for shot in shots {
            guard let when = shot.date else { continue }
            let day = calendar.startOfDay(for: when)
            if day >= start && day <= today { byDay[day, default: []].append(shot) }
        }
        var days: [CalendarDay] = []
        for index in 0..<(26 * 7) {
            guard let date = calendar.date(byAdding: .day, value: index, to: start), date <= today else { break }
            let shotsThatDay = (byDay[date] ?? []).sorted { ($0.date ?? .distantPast) < ($1.date ?? .distantPast) }
            days.append(CalendarDay(date: date, week: index / 7, weekday: index % 7, shots: shotsThatDay))
        }
        return days
    }

    /// One short tag per recipe name ("D", "DO", "S2"), the most used first, none twice.
    static func recipeTags(for shots: [ShotModel]) -> [String: String] {
        var counts: [String: Int] = [:]
        for shot in shots { counts[shot.recipe, default: 0] += 1 }
        var used = Set<String>()
        var tags: [String: String] = [:]
        for name in counts.keys.sorted(by: { (counts[$0] ?? 0, $1) > (counts[$1] ?? 0, $0) }) {
            let letters = name.uppercased().filter { $0.isLetter || $0.isNumber }
            let word = letters.isEmpty ? "?" : String(letters)
            var candidates = [String(word.prefix(1)), String(word.prefix(2)), String(word.prefix(3))]
            var extra = 2
            while candidates.allSatisfy({ used.contains($0) }) {
                candidates.append(String(word.prefix(1)) + String(extra))
                extra += 1
            }
            let tag = candidates.first { !used.contains($0) && !$0.isEmpty } ?? "?"
            used.insert(tag)
            tags[name] = tag
        }
        return tags
    }

    /// The coffee ids of `shots` in colour order: the coffee being brewed now first, then by id.
    static func coffeeOrder(shots: [ShotModel], currentCoffeeId: Int) -> [Int] {
        Array(Set(shots.map { $0.coffeeId })).sorted { a, b in
            let aNow = a == currentCoffeeId, bNow = b == currentCoffeeId
            if aNow != bNow { return aNow }
            return a < b
        }
    }

    // MARK: Time of day

    struct DayTimePoint: Identifiable, Equatable {
        var id: Int
        /// 0: today.
        var row: Int
        var hour: Double
        var coffeeId: Int
        var coffee: String
        var recipe: String
        var weightG: Double
        var date: Date
    }

    struct PowerMark: Identifiable, Equatable {
        var id: Int
        var row: Int
        var hour: Double
        var on: Bool
        var ok: Bool
        var by: String
        var outcome: String
        var date: Date
    }

    static func hourOfDay(_ date: Date, calendar: Calendar) -> Double {
        let parts = calendar.dateComponents([.hour, .minute, .second], from: date)
        return Double(parts.hour ?? 0) + Double(parts.minute ?? 0) / 60 + Double(parts.second ?? 0) / 3600
    }

    /// Days ago (0: today) of a date.
    static func rowOf(_ date: Date, now: Date, calendar: Calendar) -> Int {
        let a = calendar.startOfDay(for: date)
        let b = calendar.startOfDay(for: now)
        return calendar.dateComponents([.day], from: a, to: b).day ?? 0
    }

    static func dayTimePoints(shots: [ShotModel], days: Int, now: Date = Date(),
                              calendar: Calendar = .current) -> [DayTimePoint] {
        shots.compactMap { shot -> DayTimePoint? in
            guard let date = shot.date else { return nil }
            let row = rowOf(date, now: now, calendar: calendar)
            guard row >= 0 && row < days else { return nil }
            return DayTimePoint(id: shot.id, row: row, hour: hourOfDay(date, calendar: calendar),
                                coffeeId: shot.coffeeId, coffee: shot.coffee, recipe: shot.recipe,
                                weightG: shot.finalWeightG, date: date)
        }
    }

    static func powerMarks(events: [InsightsPowerEvent], days: Int, now: Date = Date(),
                           calendar: Calendar = .current) -> [PowerMark] {
        events.enumerated().compactMap { index, event -> PowerMark? in
            guard event.t > 0 else { return nil }
            let date = Date(timeIntervalSince1970: TimeInterval(event.t))
            let row = rowOf(date, now: now, calendar: calendar)
            guard row >= 0 && row < days else { return nil }
            return PowerMark(id: index, row: row, hour: hourOfDay(date, calendar: calendar), on: event.on,
                             ok: event.ok, by: event.by, outcome: event.outcome, date: date)
        }
    }

    /// The hours the chart spans: the hours brewed in, at least six, as whole hours.
    static func hourRange(shotHours: [Double], powerHours: [Double]) -> ClosedRange<Double> {
        let all = shotHours + powerHours
        guard let low = all.min(), let high = all.max() else { return 6...22 }
        var start = low.rounded(.down)
        var end = (high + 0.01).rounded(.up)
        if end - start < 6 {
            let middle = (start + end) / 2
            start = max(0, (middle - 3).rounded(.down))
            end = min(24, start + 6)
        }
        return start...max(end, start + 1)
    }

    // MARK: Dial-in shots per bag

    struct DialInBag: Identifiable, Equatable {
        var id: String
        var name: String
        var roastDate: Int
        var total: Int
        /// Shots up to and including the first on-time one; nil: none on time yet.
        var dial: Int?
        var onTimeAfter: Int
        var fastAfter: Int
        var slowAfter: Int
        var lastPace: Pace
        var lastError: Double
    }

    /// Bags in order (the last 12): shots until the first one on time, then how the rest ran.
    static func dialInBags(shots: [ShotModel], tolerance: Double) -> [DialInBag] {
        let timed = shots.filter { shot in
            byWeight(shot) && shot.targetTimeS > 0 && brewTime(shot) > 0 && shot.date != nil
        }.sorted { ($0.date ?? .distantPast) < ($1.date ?? .distantPast) }
        var keys: [String] = []
        var groups: [String: [ShotModel]] = [:]
        for shot in timed {
            let key = "\(shot.coffeeId)|\(shot.roastDate)"
            if groups[key] == nil { keys.append(key) }
            groups[key, default: []].append(shot)
        }
        var bags: [DialInBag] = []
        for key in keys.suffix(12) {
            guard let list = groups[key], let last = list.last else { continue }
            func paceOf(_ shot: ShotModel) -> Pace { pace(error: brewTime(shot) - shot.targetTimeS, tolerance: tolerance) }
            let firstOnTime = list.firstIndex { paceOf($0) == .onTime }
            let after = firstOnTime.map { Array(list[($0 + 1)...]) } ?? []
            let name = list[0].coffee.isEmpty ? "no coffee" : list[0].coffee
            bags.append(DialInBag(
                id: key, name: name, roastDate: list[0].roastDate, total: list.count,
                dial: firstOnTime.map { $0 + 1 },
                onTimeAfter: after.filter { paceOf($0) == .onTime }.count,
                fastAfter: after.filter { paceOf($0) == .fast }.count,
                slowAfter: after.filter { paceOf($0) == .slow }.count,
                lastPace: paceOf(last), lastError: brewTime(last) - last.targetTimeS))
        }
        return bags
    }

    /// "3 bags, 2.5 shots to dial in on average, 1 drifted off since".
    static func dialInNote(_ bags: [DialInBag]) -> String {
        let dials = bags.compactMap { $0.dial }
        let drifted = bags.filter { $0.dial != nil && $0.lastPace != .onTime }.count
        var text = "\(bags.count) bag\(bags.count == 1 ? "" : "s")"
        if !dials.isEmpty {
            text += ", " + BrewFormat.decimal(Double(dials.reduce(0, +)) / Double(dials.count), 1)
                + " shots to dial in on average"
        }
        if drifted > 0 { text += ", \(drifted) drifted off since" }
        return text
    }

    // MARK: Grind advice against target

    struct AccuracyPoint: Identifiable, Equatable {
        var index: Int
        var event: InsightsGrindEvent
        /// Brew time minus target of the shot after the change.
        var error: Double
        /// The controller's confidence in its advice at that point, 0...1.
        var confidence: Double
        var id: Int { index }
    }

    /// The accuracy events with a target time, each with its miss and the confidence as it stood (the
    /// controller's formula, core/GrindAdvisor.h adviceConfidence: the RMS of the followed misses before
    /// it, scaled by how many there were).
    static func accuracyPoints(events: [InsightsGrindEvent], tolerance: Double) -> [AccuracyPoint] {
        let list = events.filter { $0.targetS > 0 }
        func confidence(before count: Int) -> Double {
            var misses: [Double] = []
            var k = count - 1
            while k >= 0 && misses.count < 30 {
                if list[k].followed { misses.append(list[k].timeS - list[k].targetS) }
                k -= 1
            }
            if misses.count < 2 { return 0.3 }
            let rms = (misses.map { $0 * $0 }.reduce(0, +) / Double(misses.count)).squareRoot()
            let spread = max(0, min(1, 1 - rms / (3 * tolerance)))
            return spread * Double(misses.count) / Double(misses.count + 3)
        }
        return list.enumerated().map { index, event in
            AccuracyPoint(index: index, event: event, error: event.timeS - event.targetS,
                          confidence: confidence(before: index + 1))
        }
    }

    /// "On target" tile: percent and "n of m followed changes".
    static func onTargetPercent(points: [AccuracyPoint], tolerance: Double) -> (percent: Int, onTarget: Int, followed: Int)? {
        let followed = points.filter { $0.event.followed }
        guard !followed.isEmpty else { return nil }
        let on = followed.filter { abs($0.error) <= tolerance }.count
        return (Int((100 * Double(on) / Double(followed.count)).rounded()), on, followed.count)
    }

    // MARK: Taste

    static let tasteDimensionLabels = ["Acidity", "Sweetness", "Body", "Bitterness", "Finish"]

    /// "You lean towards more sweetness, fruity; away from earthy / green." (drawTaste's `itnote`)
    static func tasteNote(_ profile: InsightsTasteProfile) -> String {
        let p = profile.profile
        guard p.coffees > 0 else {
            return "No coffees rated yet. Rate the coffees you drink on the Coffees page (How you liked it) and your taste shows here."
        }
        var text = ""
        if !p.shown {
            text += "Early days: \(p.coffees) coffee\(p.coffees == 1 ? "" : "s") rated; the profile shows from 3. "
        }
        struct Lean { var index: Int; var name: String; var score: Double }
        var leans: [Lean] = []
        for (index, name) in profile.dims.enumerated() where index < p.values.count && index < p.known.count {
            let score = (p.values[index] - 0.5) * min(1, p.known[index] * 1.5)
            if abs(score) > 0.06 { leans.append(Lean(index: index, name: name, score: score)) }
        }
        func phrase(_ lean: Lean) -> String {
            lean.index < 5 ? (lean.score > 0 ? "more " : "less ") + lean.name : lean.name
        }
        let towards = leans.filter { $0.score > 0 }.sorted { $0.score > $1.score }.prefix(4).map(phrase)
        let away = leans.filter { $0.score < 0 && $0.index >= 5 }.sorted { $0.score < $1.score }.prefix(3).map(phrase)
        if !towards.isEmpty { text += "You lean towards " + towards.joined(separator: ", ") }
        if !away.isEmpty { text += (towards.isEmpty ? "You lean " : "; ") + "away from " + away.joined(separator: ", ") }
        if !towards.isEmpty || !away.isEmpty { text += "." }
        if p.shown && !p.settled { text += " (\(p.coffees) coffees rated: settles from 8)" }
        return text.trimmingCharacters(in: .whitespaces)
    }
}
