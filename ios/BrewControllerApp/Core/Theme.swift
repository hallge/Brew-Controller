// The six colour schemes of the controller (src/ui/Palette.h, read through bbw_palette), as SwiftUI
// colours by role, plus an app-only "iPhone appearance" option that follows the system light/dark.
import Observation
import SwiftUI

/// One scheme's colours by role. 0xRRGGBB values come from the firmware's own table.
struct BrewPalette: Sendable, Equatable {
    var bg: Color
    var text: Color
    var dim: Color
    var ok: Color
    var bad: Color
    var sleep: Color
    var accent: Color
    var panel: Color
    var track: Color
    var select: Color

    init(rgb: [UInt32]) {
        func color(_ index: Int) -> Color {
            let value = index < rgb.count ? rgb[index] : 0
            return Color(.sRGB, red: Double((value >> 16) & 0xFF) / 255, green: Double((value >> 8) & 0xFF) / 255,
                         blue: Double(value & 0xFF) / 255, opacity: 1)
        }
        bg = color(0); text = color(1); dim = color(2); ok = color(3); bad = color(4)
        sleep = color(5); accent = color(6); panel = color(7); track = color(8); select = color(9)
    }

    /// The palette of scheme number `theme` from the firmware's table (an unknown number gives Dark).
    static func load(theme: Int) -> BrewPalette {
        var values = [UInt32](repeating: 0, count: 10)
        values.withUnsafeMutableBufferPointer { buffer in
            if let base = buffer.baseAddress { bbw_palette(Int32(theme), base) }
        }
        return BrewPalette(rgb: values)
    }
}

/// bbw::UiTheme, in the controller's order.
enum BrewThemeID: Int, CaseIterable, Sendable, Identifiable {
    case dark = 0, light, nord, solarizedDark, dracula, highContrast

    var id: Int { rawValue }

    /// The name from the firmware's table ("Solarized Dark", "High contrast").
    var name: String { String(cString: bbw_palette_name(Int32(rawValue))) }

    /// True for the schemes with a light background.
    var isLight: Bool { self == .light }
}

/// The app's colour state. Follows the controller's `uiTheme` setting; "iPhone appearance" instead
/// follows the system light/dark (Light and Dark schemes).
@MainActor
@Observable
final class BrewTheme {
    static let shared = BrewTheme()

    private static let followSystemKey = "bbw.theme.followSystem"

    /// The scheme the controller has (`uiTheme`), 0...5.
    private(set) var controllerTheme: BrewThemeID = .dark
    /// "iPhone appearance": Light or Dark with the system, ignoring the controller's scheme.
    private(set) var followsSystem: Bool = false
    /// The system's current appearance, fed by `updateSystem(colorScheme:)` from the root view.
    private(set) var systemIsDark: Bool = true

    /// The scheme in effect.
    var current: BrewThemeID {
        guard followsSystem else { return controllerTheme }
        return systemIsDark ? .dark : .light
    }

    /// The colours in effect.
    private(set) var palette: BrewPalette = BrewPalette.load(theme: 0)

    /// What the root view passes to `.preferredColorScheme`: nil follows the system.
    var preferredColorScheme: ColorScheme? {
        if followsSystem { return nil }
        return controllerTheme.isLight ? .light : .dark
    }

    private let api: BrewAPI
    private let defaults: UserDefaults

    init(api: BrewAPI = .shared, defaults: UserDefaults = .standard) {
        self.api = api
        self.defaults = defaults
        followsSystem = defaults.bool(forKey: Self.followSystemKey)
        refreshPalette()
    }

    /// The controller's scheme changed (from `/api/screen` "theme", `/api/settings` "uiTheme", or
    /// `BrewLive.theme`).
    func controllerThemeChanged(_ number: Int) {
        let id = BrewThemeID(rawValue: number) ?? .dark
        guard id != controllerTheme else { return }
        controllerTheme = id
        refreshPalette()
    }

    /// The system appearance changed.
    func updateSystem(colorScheme: ColorScheme) {
        let dark = colorScheme == .dark
        guard dark != systemIsDark else { return }
        systemIsDark = dark
        refreshPalette()
    }

    /// Turns "iPhone appearance" on or off (stored in UserDefaults: app-only).
    func setFollowsSystem(_ follow: Bool) {
        followsSystem = follow
        defaults.set(follow, forKey: Self.followSystemKey)
        refreshPalette()
    }

    /// Chooses a controller scheme: posts it through `/api/settings` and switches at once. Turns the
    /// "iPhone appearance" option off, since a scheme was picked by hand.
    func choose(_ theme: BrewThemeID) async throws {
        if followsSystem { setFollowsSystem(false) }
        try await api.post("/api/settings", json: ["uiTheme": .int(theme.rawValue)])
        controllerThemeChanged(theme.rawValue)
    }

    private func refreshPalette() {
        palette = BrewPalette.load(theme: current.rawValue)
    }
}

// MARK: - Tones

/// The "tone" words of `/api/screen` (ok, accent, bad, dim; src/ui/ScreenText.h) as colours.
extension BrewPalette {
    func color(forTone tone: String?) -> Color {
        switch tone {
        case "ok": return ok
        case "accent": return accent
        case "bad": return bad
        case "dim": return dim
        case "sleep": return sleep
        case "text": return text
        default: return text
        }
    }
}
