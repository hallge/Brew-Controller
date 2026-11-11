// The Insights cards that are Swift Charts: roast level against grind setting, coffee age against brew
// time, dial-in shots per bag and grind advice against target. (The taste radars and the calendar are in
// their own files.) Wording from the web page.
import Charts
import SwiftUI

// MARK: - Roast level against grind setting

struct InsightsRoastGrindCard: View {
    let shots: [ShotModel]
    let calibration: Int
    let dial: BrewFormat.Dial
    let tolerance: Double

    var body: some View {
        let palette = BrewTheme.shared.palette
        let points = InsightsMath.roastGrindPoints(shots: shots, calibration: calibration, dial: dial)
        let bands = InsightsMath.onTimeBands(points: points, tolerance: tolerance)
        let levels = Set(points.map { $0.level }).sorted().map { InsightsMath.roastLevels[$0] }
        let maxAge = max(30, points.compactMap { $0.ageDays }.max() ?? 30)
        let maxFlow = max(0.1, points.map { $0.flowGS }.max() ?? 0.1)
        InsightsCard(
            title: "Roast level against grind setting",
            note: points.isEmpty ? "" : "\(points.count) shots, \(levels.count) roast level\(levels.count == 1 ? "" : "s")",
            identifier: "insightsRoastGrind"
        ) {
            if points.isEmpty {
                InsightsEmptyNote(text: "Needs shots with a roast level and a grind setting on this grinder.",
                                  identifier: "insightsRoastGrindEmpty")
            } else {
                Chart {
                    ForEach(bands) { band in
                        RectangleMark(xStart: .value("Lowest", band.low), xEnd: .value("Highest", band.high),
                                      y: .value("Roast", InsightsMath.roastLevels[band.level]),
                                      height: .ratio(0.7))
                            .foregroundStyle(palette.ok.opacity(0.2))
                    }
                    ForEach(points) { point in
                        PointMark(x: .value("Setting", point.setting),
                                  y: .value("Roast", InsightsMath.roastLevels[point.level]))
                            .foregroundStyle(InsightsColors.age(point.ageDays, maximum: maxAge).opacity(0.85))
                            .symbolSize(30 + 70 * min(1, point.flowGS / maxFlow))
                    }
                }
                .chartYScale(domain: levels)
                .chartXScale(domain: .automatic(includesZero: false))
                .frame(height: 60 + CGFloat(levels.count) * 36)
                .accessibilityIdentifier("insightsRoastGrindChart")
                HStack(spacing: 6) {
                    Text("Days off roast:").font(.caption).foregroundStyle(palette.dim)
                    ForEach([0.0, 0.5, 1.0], id: \.self) { t in
                        InsightsSwatch(color: InsightsColors.age(Int((t * Double(maxAge)).rounded()), maximum: maxAge),
                                       text: String(Int((t * Double(maxAge)).rounded())))
                    }
                    Text("larger dot: faster pour").font(.caption).foregroundStyle(palette.dim)
                }
                Text(bandsText(bands))
                    .font(.caption)
                    .foregroundStyle(palette.dim)
                InsightsExplainer(
                    summary: "One dot per shot on this grinder: its setting and roast level, coloured by the coffee's age.",
                    lines: [
                        "Blue is fresh, red is old; grey: no roast date. A larger dot poured faster.",
                        "The green band on each roast level: the settings whose shots ran on time (within the tolerance of their target time).",
                    ])
            }
        }
    }

    private func bandsText(_ bands: [InsightsMath.OnTimeBand]) -> String {
        guard !bands.isEmpty else { return "No shot on this grinder ran on time yet: no band." }
        let parts = bands.map { band -> String in
            let name = InsightsMath.roastLevels[band.level]
            let range = band.low == band.high ? dial.setting(stored(band.low)) :
                dial.setting(stored(band.low)) + "–" + dial.setting(stored(band.high))
            return "\(name) \(range) (middle \(dial.setting(stored(band.middle))), \(band.count) shot\(band.count == 1 ? "" : "s"))"
        }
        return "Ran on time at " + parts.joined(separator: ", ")
    }

    /// The axis number back to the stored setting.
    private func stored(_ axis: Double) -> Int {
        (dial.isRinged || dial.divisor <= 1) ? Int(axis.rounded()) : Int((axis * Double(dial.divisor)).rounded())
    }
}

// MARK: - Coffee age against brew time

struct InsightsAgeCard: View {
    let shots: [ShotModel]
    let tolerance: Double

    var body: some View {
        let palette = BrewTheme.shared.palette
        let heat = InsightsMath.ageHeat(shots: shots)
        InsightsCard(
            title: "Coffee age against brew time",
            note: heat.map { "\($0.total) shots" + ($0.leftOut > 0 ? ", \($0.leftOut) without a roast date or target left out" : "") } ?? "",
            identifier: "insightsAge"
        ) {
            if let heat {
                let rows = heat.rows
                let onTimeRows = (0..<rows).filter { row in
                    InsightsMath.timeEdges[row] >= -tolerance - 1e-9 && InsightsMath.timeEdges[row + 1] <= tolerance + 1e-9
                }
                Chart {
                    if let low = onTimeRows.min(), let high = onTimeRows.max() {
                        RectangleMark(xStart: .value("From", 0.0), xEnd: .value("To", Double(heat.weeks * 7)),
                                      yStart: .value("Row", Double(low)), yEnd: .value("Row end", Double(high + 1)))
                            .foregroundStyle(palette.ok.opacity(0.16))
                    }
                    ForEach(cells(heat), id: \.id) { cell in
                        RectangleMark(xStart: .value("From", Double(cell.week * 7)),
                                      xEnd: .value("To", Double(cell.week * 7 + 7)),
                                      yStart: .value("Row", Double(cell.row)),
                                      yEnd: .value("Row end", Double(cell.row + 1)))
                            .foregroundStyle(InsightsColors.heat(Double(cell.count) / Double(heat.maxCount)))
                            .annotation(position: .overlay) {
                                Text("\(cell.count)").font(.caption2).foregroundStyle(palette.text)
                            }
                    }
                }
                .chartYScale(domain: 0.0...Double(rows))
                .chartXScale(domain: 0.0...Double(heat.weeks * 7))
                .chartYAxis {
                    AxisMarks(values: (0..<rows).map { Double($0) + 0.5 }) { value in
                        AxisValueLabel {
                            if let center = value.as(Double.self) { Text(InsightsMath.rowLabel(Int(center))).font(.caption2) }
                        }
                    }
                }
                .chartXAxis {
                    AxisMarks(values: stride(from: 0.0, through: Double(heat.weeks * 7), by: 7.0).map { $0 }) { value in
                        AxisGridLine()
                        AxisValueLabel {
                            if let day = value.as(Double.self) { Text("\(Int(day)) d").font(.caption2) }
                        }
                    }
                }
                .frame(height: 280)
                .accessibilityIdentifier("insightsAgeChart")
                Text("Rows above the outlined band ran slow, rows below ran fast. Darker squares hold more shots.")
                    .font(.footnote).foregroundStyle(palette.dim)
            } else {
                InsightsEmptyNote(text: "Needs shots with a roast date and a target time.",
                                  identifier: "insightsAgeEmpty")
            }
        }
    }

    private struct Cell: Identifiable {
        var week: Int
        var row: Int
        var count: Int
        var id: String { "\(week)-\(row)" }
    }

    private func cells(_ heat: InsightsMath.AgeHeat) -> [Cell] {
        var list: [Cell] = []
        for week in 0..<heat.weeks {
            for row in 0..<heat.rows where heat.counts[week][row] > 0 {
                list.append(Cell(week: week, row: row, count: heat.counts[week][row]))
            }
        }
        return list
    }
}

// MARK: - Dial-in shots per bag

struct InsightsDialInCard: View {
    let shots: [ShotModel]
    let tolerance: Double

    private struct Segment: Identifiable {
        var id: String
        var bag: String
        var series: String
        var count: Int
    }

    private static let seriesNames = ["To the first on time", "No shot on time yet", "On time", "Fast", "Slow"]

    var body: some View {
        let palette = BrewTheme.shared.palette
        let bags = InsightsMath.dialInBags(shots: shots, tolerance: tolerance)
        InsightsCard(title: "Dial-in shots per bag", note: bags.isEmpty ? "" : InsightsMath.dialInNote(bags),
                     identifier: "insightsDialIn") {
            if bags.isEmpty {
                InsightsEmptyNote(text: "Needs shots stopped by weight with a target time.",
                                  identifier: "insightsDialInEmpty")
            } else {
                Chart {
                    ForEach(segments(bags)) { segment in
                        BarMark(x: .value("Shots", segment.count), y: .value("Bag", segment.bag))
                            .foregroundStyle(by: .value("Kind", segment.series))
                    }
                }
                .chartForegroundStyleScale(domain: InsightsDialInCard.seriesNames,
                                           range: [InsightsColors.dialIn, Color.gray, InsightsColors.onTime,
                                                   InsightsColors.fast, InsightsColors.slow])
                .chartLegend(position: .bottom)
                .frame(height: max(160, CGFloat(bags.count) * 34 + 70))
                .accessibilityIdentifier("insightsDialInChart")
                InsightsExplainer(
                    summary: "Per bag: shots to the first on-time shot, then how the rest ran.",
                    lines: [
                        "On time: within the tolerance of that shot's target. Only shots stopped by weight count.",
                        "The shots after the first on-time one are coloured by how they ran: green on time, yellow fast, red slow. Grey: no shot on time yet.",
                    ])
                if let latest = bags.last {
                    Text("Latest shot of \(latest.name): \(latest.lastPace == .onTime ? "on time" : latest.lastPace.rawValue) (\(signed(latest.lastError)) s)")
                        .font(.footnote).foregroundStyle(palette.dim)
                }
            }
        }
    }

    private func segments(_ bags: [InsightsMath.DialInBag]) -> [Segment] {
        var list: [Segment] = []
        for (index, bag) in bags.enumerated() {
            let label = "\(index + 1). \(bag.name)"
            if let dial = bag.dial {
                list.append(Segment(id: bag.id + "d", bag: label, series: "To the first on time", count: dial))
            } else {
                list.append(Segment(id: bag.id + "n", bag: label, series: "No shot on time yet", count: bag.total))
            }
            if bag.onTimeAfter > 0 { list.append(Segment(id: bag.id + "o", bag: label, series: "On time", count: bag.onTimeAfter)) }
            if bag.fastAfter > 0 { list.append(Segment(id: bag.id + "f", bag: label, series: "Fast", count: bag.fastAfter)) }
            if bag.slowAfter > 0 { list.append(Segment(id: bag.id + "s", bag: label, series: "Slow", count: bag.slowAfter)) }
        }
        return list
    }

    private func signed(_ value: Double) -> String {
        (value > 0 ? "+" : value < 0 ? "−" : "") + BrewFormat.decimal(abs(value), 1)
    }
}

// MARK: - Grind advice against target

struct InsightsGrindCard: View {
    let accuracy: InsightsGrindAccuracy?
    let dial: BrewFormat.Dial
    let coffeeName: (Int) -> String?

    var body: some View {
        let palette = BrewTheme.shared.palette
        let tolerance = accuracy?.toleranceS ?? 2
        let points = InsightsMath.accuracyPoints(events: accuracy?.events ?? [], tolerance: tolerance)
        InsightsCard(title: "Grind advice against target", note: "", identifier: "insightsGrind") {
            if let accuracy, !points.isEmpty {
                let onTarget = InsightsMath.onTargetPercent(points: points, tolerance: tolerance)
                let percent = Int((100 * accuracy.score).rounded())
                HStack(spacing: 8) {
                    InsightsTile(title: "On target",
                                 value: onTarget.map { "\($0.percent) %" } ?? "--",
                                 detail: onTarget.map { "\($0.onTarget) of \($0.followed) followed changes" } ?? "")
                    InsightsTile(title: "Typical miss",
                                 value: onTarget == nil ? "--" : BrewFormat.decimal(accuracy.rmsS, 1) + " s",
                                 detail: "from the target time")
                    InsightsTile(title: "Confidence", value: "\(percent) %",
                                 detail: percent >= 60 ? "high: bigger steps allowed" : percent >= 35 ? "medium" : "low: smaller steps")
                }
                accuracyChart(points: points, tolerance: tolerance, palette: palette)
                HStack(spacing: 8) {
                    InsightsSwatch(color: InsightsColors.onTime, text: "landed on time")
                    InsightsSwatch(color: InsightsColors.fast, text: "still fast")
                    InsightsSwatch(color: InsightsColors.slow, text: "still slow")
                    InsightsSwatch(color: palette.accent, text: "confidence")
                }
                if let last = points.last {
                    Text(detail(last))
                        .font(.footnote).foregroundStyle(palette.dim)
                }
                InsightsExplainer(
                    summary: "Each dot is a change the advice asked for, placed by how the next shot ran against its target.",
                    lines: [
                        "Inside the green band the change landed within tolerance; above it the shot still ran slow, below it fast.",
                        "Grey dots: the grinder went somewhere other than advised; they do not count.",
                        "The orange line is the confidence, from the followed changes before each one. Low confidence makes the next change smaller; high confidence allows a bigger change.",
                    ])
            } else {
                InsightsEmptyNote(text: "Shows once a shot has followed a change the advice asked for.",
                                  identifier: "insightsGrindEmpty")
            }
        }
    }

    private func accuracyChart(points: [InsightsMath.AccuracyPoint], tolerance: Double, palette: BrewPalette) -> some View {
        let limit = max(tolerance * 2, points.map { abs($0.error) }.max() ?? 0) * 1.1
        // Confidence (0...1) is drawn on the same axis, scaled to the span of the misses; the right-hand
        // axis reads it back as percent.
        func scaled(_ confidence: Double) -> Double { -limit + confidence * 2 * limit }
        return Chart {
            RectangleMark(xStart: .value("From", -0.5), xEnd: .value("To", Double(points.count) - 0.5),
                          yStart: .value("Low", -tolerance), yEnd: .value("High", tolerance))
                .foregroundStyle(palette.ok.opacity(0.14))
            ForEach(points) { point in
                LineMark(x: .value("Change", point.index), y: .value("Confidence", scaled(point.confidence)),
                         series: .value("Series", "confidence"))
                    .foregroundStyle(palette.accent)
            }
            ForEach(points) { point in
                PointMark(x: .value("Change", point.index), y: .value("Miss", point.error))
                    .foregroundStyle(point.event.followed
                        ? InsightsColors.pace(InsightsMath.pace(error: point.error, tolerance: tolerance))
                        : Color.gray.opacity(0.6))
                    .symbolSize(60)
            }
        }
        .chartYScale(domain: -limit...limit)
        .chartXScale(domain: -0.5...(Double(max(points.count, 1)) - 0.5))
        .chartXAxis(.hidden)
        .chartYAxis {
            AxisMarks(position: .leading) { value in
                AxisGridLine()
                AxisValueLabel {
                    if let seconds = value.as(Double.self) { Text((seconds > 0 ? "+" : "") + BrewFormat.decimal(seconds, 0) + " s").font(.caption2) }
                }
            }
            AxisMarks(position: .trailing, values: [-limit, 0, limit]) { value in
                AxisValueLabel {
                    if let position = value.as(Double.self) {
                        Text("\(Int(((position + limit) / (2 * limit) * 100).rounded())) %").font(.caption2)
                    }
                }
            }
        }
        .frame(height: 220)
        .accessibilityIdentifier("insightsGrindChart")
    }

    private func detail(_ point: InsightsMath.AccuracyPoint) -> String {
        let event = point.event
        let name = coffeeName(event.coffeeId) ?? "a coffee"
        var text = "Latest: \(name), advised \(dial.setting(event.from)) → \(dial.setting(event.advised))"
        if !event.followed { text += ", pulled at \(dial.setting(event.next))" }
        text += "; next shot \(BrewFormat.seconds(event.timeS)) s against \(BrewFormat.seconds(event.targetS)) s"
        return text
    }
}
