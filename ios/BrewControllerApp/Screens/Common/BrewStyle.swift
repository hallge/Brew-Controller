// Shared look of the Brew tab: the board's palette (BrewTheme) as view helpers, its keys, and its
// panels. Nothing here invents wording.
import SwiftUI

extension View {
    /// The colours in effect (the controller's scheme, or the iPhone's appearance).
    @MainActor var brewPal: BrewPalette { BrewTheme.shared.palette }
}

/// A key as the board draws it: a filled, rounded block with one label.
struct BrewKeyStyle: ButtonStyle {
    enum Kind { case panel, ok, accent, bad, track }

    var kind: Kind = .panel
    @Environment(\.isEnabled) private var isEnabled

    @MainActor
    func makeBody(configuration: Configuration) -> some View {
        let p = BrewTheme.shared.palette
        let fill: Color
        let ink: Color
        switch kind {
        case .panel: fill = p.panel; ink = p.text
        case .track: fill = p.track; ink = p.text
        case .ok: fill = p.ok; ink = p.bg
        case .accent: fill = p.accent; ink = p.bg
        case .bad: fill = p.bad; ink = p.bg
        }
        return configuration.label
            .font(.headline.weight(.semibold))
            .multilineTextAlignment(.center)
            .foregroundStyle(ink)
            .frame(maxWidth: .infinity, minHeight: 52)
            .padding(.horizontal, 12)
            .background(fill.opacity(configuration.isPressed ? 0.7 : 1), in: RoundedRectangle(cornerRadius: 14))
            .opacity(isEnabled ? 1 : 0.4)
            .contentShape(RoundedRectangle(cornerRadius: 14))
    }
}

/// The panel behind a group of rows.
struct BrewCard<Content: View>: View {
    @ViewBuilder var content: Content

    var body: some View {
        content
            .padding(14)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(BrewTheme.shared.palette.panel, in: RoundedRectangle(cornerRadius: 14))
    }
}

/// A coloured status dot (tone: ok, bad, accent, dim ...).
struct BrewDot: View {
    var tone: String
    var body: some View {
        Circle().fill(BrewTheme.shared.palette.color(forTone: tone)).frame(width: 12, height: 12)
    }
}

/// The arc that fills to the target: grey before the pump runs, red while it runs into an empty
/// cup, green from the first drop.
struct BrewShotRing: View {
    var progress: Double
    var color: Color
    var track: Color
    var lineWidth: CGFloat = 18

    var body: some View {
        ZStack {
            Circle().stroke(track, lineWidth: lineWidth)
            Circle()
                .trim(from: 0, to: max(0, min(1, progress)))
                .stroke(color, style: StrokeStyle(lineWidth: lineWidth, lineCap: .round))
                .rotationEffect(.degrees(-90))
        }
        .padding(lineWidth / 2)
    }
}
