// The shot curve and the comparison chart (Swift Charts). Weight is a solid line on the left scale; flow
// is a line on the right scale (drawn in weight units, labelled back in g/s). WebPage.h `draw` and
// `drawCompare` are the originals.
import Charts
import SwiftUI

/// One point of a plotted line.
struct ShotsPlotPoint: Identifiable {
    let id: Int
    let t: Double
    let value: Double
}

enum ShotsPlot {
    /// At most `limit` points, evenly spaced, the last one always kept.
    static func points(_ times: [Double], _ values: [Double], limit: Int = 500) -> [ShotsPlotPoint] {
        let count = min(times.count, values.count)
        guard count > 0 else { return [] }
        let stride = max(1, count / limit)
        var out: [ShotsPlotPoint] = []
        var index = 0
        while index < count {
            out.append(ShotsPlotPoint(id: index, t: times[index], value: values[index]))
            index += stride
        }
        if (count - 1) % stride != 0 {
            out.append(ShotsPlotPoint(id: count - 1, t: times[count - 1], value: values[count - 1]))
        }
        return out
    }

    /// 0, a quarter, a half, three quarters and all of `top`: where the right-hand scale is labelled.
    static func quarters(of top: Double) -> [Double] { (0...4).map { top * Double($0) / 4 } }

    /// The colours of the comparison lines, in the order shots were added.
    static let colors: [Color] = [.blue, .orange, .green, .pink, .purple, .teal, .yellow, .red]

    static func color(_ index: Int) -> Color { colors[index % colors.count] }
}

// MARK: - One shot

struct ShotsCurveChart: View {
    let curve: ShotDataModel
    /// The best-rated shot of this coffee and recipe, drawn dashed behind.
    let reference: ShotDataModel?
    let targetG: Double
    let firstDropS: Double
    let stopS: Double

    private var palette: BrewPalette { BrewTheme.shared.palette }

    var body: some View {
        let weight = ShotsPlot.points(curve.timeS, curve.weightG)
        let flow = ShotsPlot.points(curve.timeS, curve.flowGS)
        let ref = reference.map { ShotsPlot.points($0.timeS, $0.weightG) } ?? []
        let maxT = max(curve.timeS.last ?? 1, reference?.timeS.last ?? 0, 1)
        let maxW = max(targetG * 1.1, curve.weightG.max() ?? 0, reference?.weightG.max() ?? 0, 1)
        let maxF = max(1, curve.flowGS.max() ?? 1)
        let scale = maxW / maxF
        Chart {
            RuleMark(y: .value("Target", targetG))
                .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 6]))
                .foregroundStyle(palette.dim)
            if firstDropS > 0 && firstDropS <= maxT {
                RuleMark(x: .value("First drops", firstDropS))
                    .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 6]))
                    .foregroundStyle(palette.dim.opacity(0.7))
            }
            if stopS > 0 && stopS <= maxT {
                RuleMark(x: .value("Stop", stopS))
                    .lineStyle(StrokeStyle(lineWidth: 1, dash: [4, 6]))
                    .foregroundStyle(palette.dim.opacity(0.7))
            }
            ForEach(flow) { point in
                LineMark(x: .value("Time", point.t), y: .value("Flow", point.value * scale),
                         series: .value("Line", "flow"))
                    .foregroundStyle(palette.accent)
                    .lineStyle(StrokeStyle(lineWidth: 2))
            }
            ForEach(ref) { point in
                LineMark(x: .value("Time", point.t), y: .value("Best", point.value),
                         series: .value("Line", "reference"))
                    .foregroundStyle(palette.ok)
                    .lineStyle(StrokeStyle(lineWidth: 1.5, dash: [6, 5]))
            }
            ForEach(weight) { point in
                LineMark(x: .value("Time", point.t), y: .value("Weight", point.value),
                         series: .value("Line", "weight"))
                    .foregroundStyle(palette.text)
                    .lineStyle(StrokeStyle(lineWidth: 2.5))
            }
        }
        .chartXScale(domain: 0...maxT)
        .chartYScale(domain: 0...maxW)
        .chartXAxis {
            AxisMarks(values: .automatic(desiredCount: 5)) { value in
                AxisGridLine()
                AxisValueLabel {
                    if let seconds = value.as(Double.self) { Text(BrewFormat.decimal(seconds, 0) + " s") }
                }
            }
        }
        .chartYAxis {
            AxisMarks(position: .leading, values: .automatic(desiredCount: 4)) { value in
                AxisGridLine()
                AxisValueLabel {
                    if let grams = value.as(Double.self) { Text(BrewFormat.decimal(grams, 0) + " g") }
                }
            }
            AxisMarks(position: .trailing, values: ShotsPlot.quarters(of: maxW)) { value in
                AxisValueLabel {
                    if let scaled = value.as(Double.self) {
                        Text(BrewFormat.decimal(scaled / scale, 1)).foregroundStyle(palette.accent)
                    }
                }
            }
        }
        .frame(height: 220)
        .accessibilityLabel("Shot curve: weight and flow against time")
        .accessibilityIdentifier("shotsCurve")
    }
}

// MARK: - Comparison

struct ShotsCompareChart: View {
    struct Line: Identifiable {
        let id: Int
        let name: String
        let color: Color
        let weight: [ShotsPlotPoint]
        let flow: [ShotsPlotPoint]
        /// Seconds to take off the time axis ("line up at first drops").
        let offset: Double
    }

    let lines: [Line]
    let showFlow: Bool

    var body: some View {
        let maxW = max(lines.flatMap { $0.weight.map(\.value) }.max() ?? 1, 1) * 1.05
        let maxF = max(lines.flatMap { $0.flow.map(\.value) }.max() ?? 1, 1) * 1.1
        let scale = maxW / maxF
        let minT = min(lines.map { -$0.offset }.min() ?? 0, 0)
        let maxT = max(lines.compactMap { line in line.weight.last.map { $0.t - line.offset } }.max() ?? 1, 1)
        Chart {
            ForEach(lines) { line in
                ForEach(line.weight) { point in
                    LineMark(x: .value("Time", point.t - line.offset), y: .value("Weight", point.value),
                             series: .value("Line", "\(line.id)-weight"))
                        .foregroundStyle(line.color)
                        .lineStyle(StrokeStyle(lineWidth: 2))
                }
                if showFlow {
                    ForEach(line.flow) { point in
                        LineMark(x: .value("Time", point.t - line.offset), y: .value("Flow", point.value * scale),
                                 series: .value("Line", "\(line.id)-flow"))
                            .foregroundStyle(line.color)
                            .lineStyle(StrokeStyle(lineWidth: 1.5, dash: [5, 4]))
                    }
                }
            }
        }
        .chartXScale(domain: minT...maxT)
        .chartYScale(domain: 0...maxW)
        .chartXAxis {
            AxisMarks(values: .automatic(desiredCount: 5)) { value in
                AxisGridLine()
                AxisValueLabel {
                    if let seconds = value.as(Double.self) { Text(BrewFormat.decimal(seconds, 0) + " s") }
                }
            }
        }
        .chartYAxis {
            AxisMarks(position: .leading, values: .automatic(desiredCount: 4)) { value in
                AxisGridLine()
                AxisValueLabel {
                    if let grams = value.as(Double.self) { Text(BrewFormat.decimal(grams, 0) + " g") }
                }
            }
            AxisMarks(position: .trailing, values: showFlow ? ShotsPlot.quarters(of: maxW) : []) { value in
                AxisValueLabel {
                    if let scaled = value.as(Double.self) { Text(BrewFormat.decimal(scaled / scale, 1)) }
                }
            }
        }
        .frame(height: 260)
        .accessibilityLabel("Compared shots: weight and flow against time")
        .accessibilityIdentifier("shotsCompareChart")
    }
}
