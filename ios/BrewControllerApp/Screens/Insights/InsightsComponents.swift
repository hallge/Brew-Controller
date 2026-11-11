// Small pieces the Insights cards share: the card, the empty note, the colours of the charts and the
// radar chart (Swift Charts has no radar, so it is drawn with paths).
import Charts
import SwiftUI

/// The colours of the charts. "On time", "fast" and "slow" follow the board's ok, warning and bad colours.
@MainActor
enum InsightsColors {
    static var palette: BrewPalette { BrewTheme.shared.palette }

    static var onTime: Color { palette.ok }
    static var fast: Color { Color(.sRGB, red: 0.96, green: 0.77, blue: 0.32, opacity: 1) }
    static var slow: Color { palette.bad }
    static var dialIn: Color { palette.accent }

    static func pace(_ pace: InsightsMath.Pace) -> Color {
        switch pace {
        case .onTime: return onTime
        case .fast: return fast
        case .slow: return slow
        }
    }

    /// Days off roast as a ramp from blue (fresh) to red (old); grey when the roast date is not known.
    static func age(_ days: Int?, maximum: Int) -> Color {
        guard let days else { return Color.gray }
        let t = min(1, Double(days) / Double(max(1, maximum)))
        return Color(hue: (1 - t) * 0.6, saturation: 0.7, brightness: 0.92)
    }

    /// More shots, darker orange.
    static func heat(_ fraction: Double) -> Color {
        palette.accent.opacity(0.2 + 0.8 * min(1, max(0, fraction)))
    }

    private static let coffeeHex: [UInt32] = [0xF59E0B, 0x38BDF8, 0xA78BFA, 0xF472B6, 0x34D399, 0xFACC15,
                                              0xFB7185, 0x60A5FA, 0x2DD4BF, 0xC084FC]

    /// The colour of the n-th coffee (the one being brewed now is first).
    static func coffee(_ index: Int) -> Color {
        let value = coffeeHex[((index % coffeeHex.count) + coffeeHex.count) % coffeeHex.count]
        return Color(.sRGB, red: Double((value >> 16) & 0xFF) / 255, green: Double((value >> 8) & 0xFF) / 255,
                     blue: Double(value & 0xFF) / 255, opacity: 1)
    }
}

/// A card with a title, a note on the right and its content.
struct InsightsCard<Content: View>: View {
    let title: String
    var note: String = ""
    let identifier: String
    @ViewBuilder let content: () -> Content

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 10) {
            Text(title)
                .font(.headline)
                .foregroundStyle(palette.text)
                .accessibilityAddTraits(.isHeader)
            if !note.isEmpty {
                Text(note).font(.footnote).foregroundStyle(palette.dim)
            }
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(palette.panel, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(identifier)
    }
}

/// What a chart says when there is nothing to draw yet.
struct InsightsEmptyNote: View {
    let text: String
    let identifier: String

    var body: some View {
        Text(text)
            .font(.subheadline)
            .foregroundStyle(BrewTheme.shared.palette.dim)
            .frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityIdentifier(identifier)
    }
}

/// A collapsed explanation under a chart (the web page's "note" details).
struct InsightsExplainer: View {
    let summary: String
    let lines: [String]

    var body: some View {
        DisclosureGroup {
            VStack(alignment: .leading, spacing: 6) {
                ForEach(lines.indices, id: \.self) { index in
                    Text("• " + lines[index]).font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        } label: {
            Text(summary).font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
        }
    }
}

/// A colour swatch and a word, for a legend.
struct InsightsSwatch: View {
    let color: Color
    let text: String

    var body: some View {
        HStack(spacing: 4) {
            RoundedRectangle(cornerRadius: 3).fill(color).frame(width: 12, height: 12)
            Text(text).font(.caption).foregroundStyle(BrewTheme.shared.palette.text)
        }
    }
}

/// One tile: a small title, a big value and a line under it.
struct InsightsTile: View {
    let title: String
    let value: String
    let detail: String

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 2) {
            Text(title).font(.caption).foregroundStyle(palette.dim)
            Text(value).font(.title3.weight(.semibold)).foregroundStyle(palette.text)
            Text(detail).font(.caption2).foregroundStyle(palette.dim)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(palette.track.opacity(0.5), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
    }
}

// MARK: - Radar

/// A radar chart: one spoke per label, the middle ring is "no lean either way", further out is more.
/// A pale dot says little is known about that value yet.
struct InsightsRadar: View {
    let labels: [String]
    let values: [Double]
    let known: [Double]

    private func point(_ index: Int, _ radius: Double, center: CGPoint) -> CGPoint {
        let angle = 2 * Double.pi * Double(index) / Double(max(1, labels.count))
        return CGPoint(x: center.x + radius * sin(angle), y: center.y - radius * cos(angle))
    }

    private func ring(_ fraction: Double, center: CGPoint, radius: Double) -> Path {
        var path = Path()
        for index in 0...labels.count {
            let p = point(index % max(1, labels.count), radius * fraction, center: center)
            if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
        }
        return path
    }

    private func shape(center: CGPoint, radius: Double) -> Path {
        var path = Path()
        for index in labels.indices {
            let value = index < values.count ? min(1, max(0, values[index])) : 0.5
            let p = point(index, radius * value, center: center)
            if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
        }
        path.closeSubpath()
        return path
    }

    var body: some View {
        let palette = BrewTheme.shared.palette
        GeometryReader { geometry in
            let center = CGPoint(x: geometry.size.width / 2, y: geometry.size.height / 2)
            let radius = max(10, min(geometry.size.width, geometry.size.height) / 2 - 34)
            ZStack {
                ForEach([0.25, 0.5, 0.75, 1.0], id: \.self) { fraction in
                    ring(fraction, center: center, radius: radius)
                        .stroke(fraction == 0.5 ? palette.dim : palette.track, lineWidth: fraction == 0.5 ? 1.5 : 1)
                }
                ForEach(labels.indices, id: \.self) { index in
                    Path { path in
                        path.move(to: center)
                        path.addLine(to: point(index, radius, center: center))
                    }
                    .stroke(palette.track, lineWidth: 1)
                    let isKnown = index < known.count && known[index] > 0.05
                    Text(labels[index])
                        .font(.caption2)
                        .foregroundStyle(isKnown ? palette.text : palette.dim)
                        .position(point(index, radius + 22, center: center))
                }
                shape(center: center, radius: radius).fill(palette.accent.opacity(0.22))
                shape(center: center, radius: radius).stroke(palette.accent, lineWidth: 2)
                ForEach(labels.indices, id: \.self) { index in
                    let value = index < values.count ? min(1, max(0, values[index])) : 0.5
                    let certainty = index < known.count ? min(1, max(0, known[index])) : 0
                    Circle()
                        .fill(palette.accent.opacity(0.35 + 0.65 * certainty))
                        .frame(width: 8, height: 8)
                        .position(point(index, radius * value, center: center))
                }
            }
        }
        .aspectRatio(1.25, contentMode: .fit)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(accessibilityText)
    }

    private var accessibilityText: String {
        labels.indices.map { index in
            let value = index < values.count ? Int((values[index] * 100).rounded()) : 50
            return "\(labels[index]) \(value) percent"
        }.joined(separator: ", ")
    }
}

// MARK: - Tap tooltips

/// Where a shot opens from the charts (pushed by InsightsScreen).
struct InsightsShotTarget: Identifiable, Hashable {
    let id: Int
}

extension View {
    /// Calls `handler` with the tapped point, relative to the chart's plot area, and the chart's proxy.
    func insightsTap(_ handler: @escaping (_ point: CGPoint, _ proxy: ChartProxy) -> Void) -> some View {
        chartOverlay { proxy in
            GeometryReader { geometry in
                Rectangle().fill(Color.clear).contentShape(Rectangle())
                    .onTapGesture { location in
                        guard let frame = proxy.plotFrame else { return }
                        let origin = geometry[frame].origin
                        handler(CGPoint(x: location.x - origin.x, y: location.y - origin.y), proxy)
                    }
            }
        }
    }
}

enum InsightsTap {
    /// The id of the nearest candidate within `distance` points of `point` (nil when none is close).
    static func nearest(_ candidates: [(id: Int, position: CGPoint)], to point: CGPoint,
                        within distance: CGFloat = 30) -> Int? {
        var best: (id: Int, distance: CGFloat)?
        for candidate in candidates {
            let dx = candidate.position.x - point.x
            let dy = candidate.position.y - point.y
            let d = (dx * dx + dy * dy).squareRoot()
            if d <= distance && (best == nil || d < (best?.distance ?? .infinity)) { best = (candidate.id, d) }
        }
        return best?.id
    }
}

/// The line a tapped chart shows under it, with a hint when a second tap opens something.
struct InsightsTooltip: View {
    let text: String
    var hint = ""
    let identifier: String

    var body: some View {
        if !text.isEmpty {
            let palette = BrewTheme.shared.palette
            VStack(alignment: .leading, spacing: 2) {
                Text(text).font(.footnote).foregroundStyle(palette.text)
                if !hint.isEmpty { Text(hint).font(.caption).foregroundStyle(palette.dim) }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(8)
            .background(palette.track.opacity(0.5), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier(identifier)
        }
    }
}
