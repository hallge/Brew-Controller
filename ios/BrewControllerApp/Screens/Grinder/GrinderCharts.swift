// The two grinder charts (web: drawScatter, the shot-by-shot chart): both read /api/shots, which Core's
// BrewAPI.shots gives. Shots count when they were pulled on this grinder's calibration, stopped on weight
// and were not set to Ignore.
import Charts
import SwiftUI

/// One shot as the charts use it.
struct GrinderPoint: Identifiable, Equatable {
    var id: Int
    var order: Int
    var time: Double
    var setting: Int
    var rating: Int
    var coffeeId: Int
    var coffee: String
    var roastDate: Int
    var timestamp: String
}

enum GrinderChartData {
    /// The shots the charts show, oldest first.
    static func points(_ shots: [ShotModel], calibration: Int, pourBasis: Bool) -> [GrinderPoint] {
        let kept = shots
            .filter { $0.grinderCalibration == calibration && $0.endReason.hasPrefix("weight") && $0.grindVote != "ignore" && $0.grindUm > 0 }
            .reversed()
        var result: [GrinderPoint] = []
        for shot in kept {
            let time = pourBasis ? shot.pourTimeS : shot.shotTimeS
            guard time > 0 else { continue }
            result.append(GrinderPoint(id: shot.id, order: result.count, time: time, setting: shot.grindUm, rating: shot.rating,
                                       coffeeId: shot.coffeeId, coffee: shot.coffee.isEmpty ? "no coffee" : shot.coffee,
                                       roastDate: shot.roastDate, timestamp: shot.timestamp))
        }
        return result
    }

    /// The colours, one per coffee, the same in both charts (the web's coffeeColours): the current coffee first
    /// and in the accent colour, the others in fixed order.
    static func coffeeStyle(_ points: [GrinderPoint], current: String) -> (domain: [String], range: [Color]) {
        let palette: [Color] = [.blue, .purple, .pink, .teal, .brown, .indigo, .mint, .cyan, .gray]
        var names: [String] = []
        for point in points where !names.contains(point.coffee) { names.append(point.coffee) }
        names.sort { a, b in
            if (a == current) != (b == current) { return a == current }
            return a.localizedCaseInsensitiveCompare(b) == .orderedAscending
        }
        var others = 0
        let colors: [Color] = names.map { name in
            if name == current { return .orange }
            defer { others += 1 }
            return palette[others % palette.count]
        }
        return (names, colors)
    }

    /// The nearest point to a position on the chart, by the given distance.
    static func nearest(_ points: [GrinderPoint], to value: Double, by key: (GrinderPoint) -> Double) -> GrinderPoint? {
        points.min { abs(key($0) - value) < abs(key($1) - value) }
    }
}

private func chartSummary(_ p: GrinderPoint, dial: BrewFormat.Dial) -> String {
    "\(p.coffee): \(dial.settingUnit(p.setting)), \(BrewFormat.seconds(p.time)) s"
        + (p.rating > 0 ? ", \(p.rating) star\(p.rating == 1 ? "" : "s")" : "")
}

/// The shot a dot stands for: its summary, and a link that opens it.
struct GrinderPickedShot: View {
    var point: GrinderPoint
    var dial: BrewFormat.Dial
    var id: String

    var body: some View {
        NavigationLink {
            ShotsDetailScreen(shotID: point.id)
        } label: {
            HStack {
                Text(chartSummary(point, dial: dial)).font(.footnote)
                Spacer()
                Text("Open the shot").font(.footnote).foregroundStyle(.tint)
            }
        }
        .accessibilityIdentifier(id)
    }
}

/// Brew time against grind setting, every coffee.
struct GrinderScatterChart: View {
    var points: [GrinderPoint]
    var dial: BrewFormat.Dial
    var targetS: Double
    var toleranceS: Double
    var currentCoffee: String
    @State private var picked: Double?

    private var selection: GrinderPoint? {
        guard let picked else { return nil }
        return GrinderChartData.nearest(points, to: picked) { Double($0.setting) }
    }

    private var style: (domain: [String], range: [Color]) { GrinderChartData.coffeeStyle(points, current: currentCoffee) }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Chart {
                if targetS > 0 {
                    RectangleMark(yStart: .value("Fast", targetS - toleranceS), yEnd: .value("Slow", targetS + toleranceS))
                        .foregroundStyle(.green.opacity(0.15))
                }
                ForEach(points) { point in
                    PointMark(x: .value("Setting", Double(point.setting)), y: .value("Seconds", point.time))
                        .foregroundStyle(by: .value("Coffee", point.coffee))
                        .symbolSize((point.rating >= 4 ? 90 : 40) * (point.coffee == currentCoffee ? 1.5 : 1))
                        .opacity(point.coffee == currentCoffee ? 1 : 0.8)
                }
                if let selection {
                    PointMark(x: .value("Setting", Double(selection.setting)), y: .value("Seconds", selection.time))
                        .symbol(.circle.strokeBorder(lineWidth: 2))
                        .foregroundStyle(.primary)
                        .symbolSize(160)
                }
            }
            .chartForegroundStyleScale(domain: style.domain, range: style.range)
            .chartLegend(position: .bottom, alignment: .leading, spacing: 8)
            .chartXAxis {
                AxisMarks { value in
                    AxisGridLine()
                    AxisValueLabel {
                        if let stored = value.as(Double.self) { Text(dial.setting(Int(stored.rounded()))) }
                    }
                }
            }
            .chartYAxisLabel("seconds")
            .chartXSelection(value: $picked)
            .frame(height: 220)
            .accessibilityIdentifier("grinderScatterChart")
            if let selection {
                GrinderPickedShot(point: selection, dial: dial, id: "grinderScatterPicked")
            }
        }
    }
}

/// The grind setting shot by shot: a line per coffee, dots coloured by time against the target, rings for
/// 4 and 5 stars, a dashed line where a new bag (a new roast date) starts.
struct GrinderShotsChart: View {
    var points: [GrinderPoint]
    var dial: BrewFormat.Dial
    var targetS: Double
    var toleranceS: Double
    var currentCoffee = ""
    @State private var picked: Double?

    private var selection: GrinderPoint? {
        guard let picked else { return nil }
        return GrinderChartData.nearest(points, to: picked) { Double($0.order) }
    }

    private var style: (domain: [String], range: [Color]) { GrinderChartData.coffeeStyle(points, current: currentCoffee) }

    private func color(_ time: Double) -> Color {
        guard targetS > 0 else { return .secondary }
        return abs(time - targetS) <= toleranceS ? .green : (time < targetS ? .orange : .red)
    }

    /// Orders where a coffee's roast date changes from the shot before it of the same coffee.
    private var newBags: [Int] {
        var last: [Int: Int] = [:]
        var found: [Int] = []
        for point in points {
            if let before = last[point.coffeeId], before != point.roastDate, point.roastDate > 0 { found.append(point.order) }
            last[point.coffeeId] = point.roastDate
        }
        return found
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Chart {
                ForEach(newBags, id: \.self) { order in
                    RuleMark(x: .value("New bag", Double(order)))
                        .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 3]))
                        .foregroundStyle(.secondary)
                }
                ForEach(points) { point in
                    LineMark(x: .value("Shot", Double(point.order)), y: .value("Setting", Double(point.setting)))
                        .foregroundStyle(by: .value("Coffee", point.coffee))
                        .interpolationMethod(.stepEnd)
                    PointMark(x: .value("Shot", Double(point.order)), y: .value("Setting", Double(point.setting)))
                        .foregroundStyle(color(point.time))
                        .symbolSize(point.rating >= 4 ? 90 : 36)
                    if point.rating >= 4 {
                        PointMark(x: .value("Shot", Double(point.order)), y: .value("Setting", Double(point.setting)))
                            .symbol(.circle.strokeBorder(lineWidth: 2))
                            .foregroundStyle(.yellow)
                            .symbolSize(150)
                    }
                }
            }
            .chartForegroundStyleScale(domain: style.domain, range: style.range)
            .chartLegend(position: .bottom, alignment: .leading, spacing: 8)
            .chartYAxis {
                AxisMarks { value in
                    AxisGridLine()
                    AxisValueLabel {
                        if let stored = value.as(Double.self) { Text(dial.setting(Int(stored.rounded()))) }
                    }
                }
            }
            .chartXAxisLabel("shots, oldest to newest")
            .chartXSelection(value: $picked)
            .frame(height: 220)
            .accessibilityIdentifier("grinderShotsChart")
            if let selection {
                GrinderPickedShot(point: selection, dial: dial, id: "grinderShotsPicked")
            }
            Text("Dots: green on time, orange fast, red slow. Rings: 4 and 5 stars. Dashed: a new bag.")
                .font(.caption2).foregroundStyle(.secondary)
        }
    }
}
