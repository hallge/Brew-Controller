// The brewing calendar: the last 26 weeks, a column a week, in four modes (web page: drawCalendar and
// drawDayTimes). Shots a day, which coffee and recipe, how each day drifted from the target, and a
// weekday-by-hour view of when shots are pulled with the machine's power presses.
import Charts
import SwiftUI

struct InsightsCalendarCard: View {
    let shots: [ShotModel]
    let powerEvents: [InsightsPowerEvent]
    let tolerance: Double
    let currentCoffeeId: Int

    @State private var mode: InsightsMath.CalendarMode = .shots
    @State private var daySpan = 14
    @State private var detail = ""
    /// The day or shot a first tap picked; a second tap on it opens the shot.
    @State private var selectedDay: Date?
    @State private var selectedShot: Int?
    /// Opens a shot (a second tap).
    var onOpen: (Int) -> Void = { _ in }

    var body: some View {
        let palette = BrewTheme.shared.palette
        let dated = shots.filter { $0.date != nil }
        InsightsCard(title: "Brewing calendar", note: "", identifier: "insightsCalendar") {
            if dated.isEmpty {
                InsightsEmptyNote(text: "No dated shots yet.", identifier: "insightsCalendarEmpty")
            } else {
                Picker("What the calendar shows", selection: $mode) {
                    ForEach(InsightsMath.CalendarMode.allCases) { mode in
                        Text(mode.title).tag(mode)
                    }
                }
                .pickerStyle(.segmented)
                .accessibilityIdentifier("insightsCalendarMode")
                .onChange(of: mode) { _, _ in
                    detail = ""
                    selectedDay = nil
                    selectedShot = nil
                }
                if mode == .hours {
                    hoursChart(shots: dated, palette: palette)
                } else {
                    gridChart(shots: dated, palette: palette)
                }
                InsightsTooltip(text: detail,
                                hint: (selectedDay != nil || selectedShot != nil) ? "Tap again to open the shot." : "",
                                identifier: "insightsCalendarDetail")
            }
        }
    }

    // MARK: Weeks by weekday

    private func gridChart(shots: [ShotModel], palette: BrewPalette) -> some View {
        let days = InsightsMath.calendarDays(shots: shots)
        let inRange = days.flatMap { $0.shots }
        let order = InsightsMath.coffeeOrder(shots: inRange, currentCoffeeId: currentCoffeeId)
        let colors = Dictionary(uniqueKeysWithValues: order.enumerated().map { ($0.element, InsightsColors.coffee($0.offset)) })
        let tags = InsightsMath.recipeTags(for: inRange)
        let maxCount = max(1, days.map { $0.count }.max() ?? 1)
        let monthLabels = monthLabels(days)
        let weekdayNames = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
        return VStack(alignment: .leading, spacing: 8) {
            ScrollView(.horizontal, showsIndicators: false) {
                Chart {
                    ForEach(days) { day in
                        let bottom = Double(6 - day.weekday)
                        if mode == .brew && day.count > 0 {
                            let list = Array(day.shots.prefix(4))
                            let stripes = Double(list.count)
                            let left = Double(day.week) + 0.04
                            ForEach(Array(list.enumerated()), id: \.offset) { entry in
                                let from = left + 0.92 * Double(entry.offset) / stripes
                                let to = left + 0.92 * Double(entry.offset + 1) / stripes
                                RectangleMark(xStart: .value("From", from), xEnd: .value("To", to),
                                              yStart: .value("Day", bottom + 0.04), yEnd: .value("Day end", bottom + 0.96))
                                    .foregroundStyle(colors[entry.element.coffeeId] ?? palette.dim)
                            }
                            RectangleMark(xStart: .value("From", Double(day.week)), xEnd: .value("To", Double(day.week) + 1),
                                          yStart: .value("Day", bottom), yEnd: .value("Day end", bottom + 1))
                                .foregroundStyle(Color.clear)
                                .annotation(position: .overlay) {
                                    Text(Set(day.shots.map { tags[$0.recipe] ?? "?" }).sorted().joined(separator: " "))
                                        .font(.system(size: 7, weight: .bold))
                                        .foregroundStyle(Color.black)
                                }
                        } else {
                            RectangleMark(xStart: .value("From", Double(day.week) + 0.04),
                                          xEnd: .value("To", Double(day.week) + 0.96),
                                          yStart: .value("Day", bottom + 0.04), yEnd: .value("Day end", bottom + 0.96))
                                .foregroundStyle(cellColor(day, maxCount: maxCount, palette: palette))
                        }
                    }
                }
                .chartXScale(domain: 0.0...26.0)
                .chartYScale(domain: 0.0...7.0)
                .chartXAxis {
                    AxisMarks(values: monthLabels.keys.sorted().map { Double($0) + 0.5 }) { value in
                        AxisValueLabel {
                            if let center = value.as(Double.self), let label = monthLabels[Int(center)] {
                                Text(label).font(.caption2)
                            }
                        }
                    }
                }
                .chartYAxis {
                    AxisMarks(values: [0.5, 2.5, 4.5, 6.5]) { value in
                        AxisValueLabel {
                            if let center = value.as(Double.self) {
                                Text(weekdayNames[6 - Int(center)]).font(.caption2)
                            }
                        }
                    }
                }
                .chartOverlay { proxy in
                    GeometryReader { geometry in
                        Rectangle().fill(Color.clear).contentShape(Rectangle())
                            .onTapGesture { location in
                                guard let frame = proxy.plotFrame else { return }
                                let origin = geometry[frame].origin
                                guard let x = proxy.value(atX: location.x - origin.x, as: Double.self),
                                      let y = proxy.value(atY: location.y - origin.y, as: Double.self) else { return }
                                let week = Int(x.rounded(.down))
                                let weekday = 6 - Int(y.rounded(.down))
                                if let day = days.first(where: { $0.week == week && $0.weekday == weekday }) {
                                    if selectedDay == day.date, let last = day.shots.last {
                                        onOpen(last.id)
                                        return
                                    }
                                    selectedDay = day.count > 0 ? day.date : nil
                                    detail = describe(day)
                                } else {
                                    selectedDay = nil
                                    detail = ""
                                }
                            }
                    }
                }
                .frame(width: 560, height: 190)
                .accessibilityIdentifier("insightsCalendarChart")
            }
            .defaultScrollAnchor(.trailing)
            legend(mode: mode, maxCount: maxCount, order: order, colors: colors, tags: tags, inRange: inRange, palette: palette)
        }
    }

    private func cellColor(_ day: InsightsMath.CalendarDay, maxCount: Int, palette: BrewPalette) -> Color {
        guard day.count > 0 else { return palette.track.opacity(0.4) }
        if mode == .drift {
            guard let drift = day.drift else { return palette.dim }
            let pace = InsightsMath.pace(error: drift, tolerance: tolerance)
            let strength = pace == .onTime ? 1.0 : 0.35 + 0.65 * min(1, abs(drift) / (3 * tolerance))
            return InsightsColors.pace(pace).opacity(strength)
        }
        return InsightsColors.heat(Double(day.count) / Double(maxCount))
    }

    private func monthLabels(_ days: [InsightsMath.CalendarDay]) -> [Int: String] {
        var labels: [Int: String] = [:]
        var last = -1
        let formatter = DateFormatter()
        formatter.setLocalizedDateFormatFromTemplate("MMM")
        for day in days where day.weekday == 0 {
            let month = Calendar.current.component(.month, from: day.date)
            if month != last {
                last = month
                labels[day.week] = formatter.string(from: day.date)
            }
        }
        return labels
    }

    private func describe(_ day: InsightsMath.CalendarDay) -> String {
        let when = day.date.formatted(.dateTime.weekday(.abbreviated).month(.abbreviated).day())
        guard day.count > 0 else { return "\(when): no shots" }
        switch mode {
        case .shots:
            return "\(when): \(day.count) shot\(day.count == 1 ? "" : "s")"
        case .brew:
            let lines = day.shots.map { shot -> String in
                let time = shot.date?.formatted(.dateTime.hour().minute()) ?? ""
                return "\(time) \(shot.coffee.isEmpty ? "no coffee" : shot.coffee) · \(shot.recipe.isEmpty ? "no recipe" : shot.recipe)"
            }
            return "\(when)\n" + lines.joined(separator: "\n")
        case .drift:
            if let drift = day.drift {
                return "\(when): \(day.count) shot\(day.count == 1 ? "" : "s"), brew time \(drift > 0 ? "+" : "")\(BrewFormat.decimal(drift, 1)) s from target on average"
            }
            return "\(when): \(day.count) shot\(day.count == 1 ? "" : "s"), none timed against a target"
        case .hours:
            return when
        }
    }

    @ViewBuilder
    private func legend(mode: InsightsMath.CalendarMode, maxCount: Int, order: [Int], colors: [Int: Color],
                        tags: [String: String], inRange: [ShotModel], palette: BrewPalette) -> some View {
        switch mode {
        case .shots:
            HStack(spacing: 8) {
                Text("Shots a day:").font(.caption).foregroundStyle(palette.dim)
                InsightsSwatch(color: InsightsColors.heat(1.0 / Double(maxCount)), text: "1")
                InsightsSwatch(color: InsightsColors.heat(1), text: "\(maxCount)")
            }
        case .drift:
            HStack(spacing: 8) {
                Text("Average brew time against target:").font(.caption).foregroundStyle(palette.dim)
                InsightsSwatch(color: InsightsColors.onTime, text: "on time")
                InsightsSwatch(color: InsightsColors.fast, text: "fast")
                InsightsSwatch(color: InsightsColors.slow, text: "slow")
            }
        case .brew:
            VStack(alignment: .leading, spacing: 4) {
                ForEach(order, id: \.self) { id in
                    InsightsSwatch(color: colors[id] ?? palette.dim,
                                   text: (inRange.first { $0.coffeeId == id }?.coffee).flatMap { $0.isEmpty ? nil : $0 } ?? "no coffee")
                }
                Text("Recipes: " + tags.sorted { $0.value < $1.value }
                    .map { "\($0.value) \($0.key.isEmpty ? "no recipe" : $0.key)" }.joined(separator: ", "))
                    .font(.caption).foregroundStyle(palette.dim)
                Text("One stripe per shot, up to 4 a day.").font(.caption).foregroundStyle(palette.dim)
            }
        case .hours:
            EmptyView()
        }
    }

    // MARK: Time of day

    private func hoursChart(shots: [ShotModel], palette: BrewPalette) -> some View {
        let span = daySpan
        let points = InsightsMath.dayTimePoints(shots: shots, days: span)
        let marks = InsightsMath.powerMarks(events: powerEvents, days: span)
        let range = InsightsMath.hourRange(shotHours: points.map { $0.hour }, powerHours: marks.map { $0.hour })
        let order = InsightsMath.coffeeOrder(shots: shots.filter { shot in points.contains { $0.id == shot.id } },
                                             currentCoffeeId: currentCoffeeId)
        let colors = Dictionary(uniqueKeysWithValues: order.enumerated().map { ($0.element, InsightsColors.coffee($0.offset)) })
        let tags = InsightsMath.recipeTags(for: shots.filter { shot in points.contains { $0.id == shot.id } })
        let rowHeight: CGFloat = span <= 14 ? 24 : span <= 30 ? 19 : 12
        let step = span <= 14 ? 1 : span <= 30 ? 2 : 7
        return VStack(alignment: .leading, spacing: 8) {
            Picker("Days", selection: $daySpan) {
                ForEach([14, 30, 90], id: \.self) { days in Text("\(days) days").tag(days) }
            }
            .pickerStyle(.segmented)
            .accessibilityIdentifier("insightsCalendarSpan")
            Chart {
                ForEach(marks) { mark in
                    PointMark(x: .value("Hour", mark.hour), y: .value("Day", Double(span - 1 - mark.row)))
                        .symbol {
                            Image(systemName: mark.on ? "arrowtriangle.up.fill" : "arrowtriangle.down.fill")
                                .font(.system(size: 9))
                                .foregroundStyle(mark.on ? InsightsColors.onTime : palette.dim)
                                .opacity(mark.ok ? 1 : 0.4)
                        }
                }
                ForEach(points) { point in
                    PointMark(x: .value("Hour", point.hour), y: .value("Day", Double(span - 1 - point.row)))
                        .foregroundStyle(colors[point.coffeeId] ?? palette.dim)
                        .symbolSize(span <= 14 ? 90 : span <= 30 ? 50 : 20)
                        .annotation(position: .overlay) {
                            if span <= 30 {
                                Text(tags[point.recipe] ?? "?")
                                    .font(.system(size: 7, weight: .bold))
                                    .foregroundStyle(Color.black)
                            }
                        }
                }
            }
            .chartXScale(domain: range)
            .chartYScale(domain: -0.5...(Double(span) - 0.5))
            .chartXAxis {
                AxisMarks(values: .automatic(desiredCount: 6)) { value in
                    AxisGridLine()
                    AxisValueLabel {
                        if let hour = value.as(Double.self) { Text(String(format: "%02d:00", Int(hour) % 24)).font(.caption2) }
                    }
                }
            }
            .chartYAxis {
                AxisMarks(values: stride(from: 0, to: span, by: step).map { Double(span - 1 - $0) }) { value in
                    AxisGridLine()
                    AxisValueLabel {
                        if let position = value.as(Double.self) {
                            Text(dayLabel(row: span - 1 - Int(position))).font(.caption2)
                        }
                    }
                }
            }
            .insightsTap { location, proxy in
                let candidates = points.compactMap { point -> (id: Int, position: CGPoint)? in
                    guard let x = proxy.position(forX: point.hour),
                          let y = proxy.position(forY: Double(span - 1 - point.row)) else { return nil }
                    return (id: point.id, position: CGPoint(x: x, y: y))
                }
                guard let id = InsightsTap.nearest(candidates, to: location, within: 24),
                      let point = points.first(where: { $0.id == id }) else {
                    selectedShot = nil
                    detail = ""
                    return
                }
                if selectedShot == id {
                    onOpen(id)
                    return
                }
                selectedShot = id
                let time = point.date.formatted(.dateTime.weekday(.abbreviated).month(.abbreviated).day().hour().minute())
                detail = time + " · " + (point.coffee.isEmpty ? "no coffee" : point.coffee) + " · "
                    + (point.recipe.isEmpty ? "no recipe" : point.recipe)
                    + (point.weightG > 0 ? ", " + BrewFormat.grams(point.weightG) + " g" : "")
            }
            .frame(height: CGFloat(span) * rowHeight + 40)
            .accessibilityIdentifier("insightsCalendarChart")
            HStack(spacing: 8) {
                ForEach(order, id: \.self) { id in
                    InsightsSwatch(color: colors[id] ?? palette.dim,
                                   text: (shots.first { $0.coffeeId == id }?.coffee).flatMap { $0.isEmpty ? nil : $0 } ?? "no coffee")
                }
            }
            if points.isEmpty {
                Text("No shots in these days").font(.caption).foregroundStyle(palette.dim)
            } else {
                Text("Recipes: " + tags.sorted { $0.value < $1.value }
                    .map { "\($0.value) \($0.key.isEmpty ? "no recipe" : $0.key)" }.joined(separator: ", "))
                    .font(.caption).foregroundStyle(palette.dim)
            }
            Text("▲ machine on  ▼ off" + (marks.isEmpty ? " (none logged in these days)" : ""))
                .font(.caption).foregroundStyle(palette.dim)
        }
    }

    private func dayLabel(row: Int) -> String {
        let date = Calendar.current.date(byAdding: .day, value: -row, to: Date()) ?? Date()
        return date.formatted(.dateTime.weekday(.abbreviated).day().month(.abbreviated))
    }
}
