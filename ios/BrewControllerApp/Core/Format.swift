// Number and text formats that match the board's units, so the app and the glass read alike.
import Foundation

enum BrewFormat {
    // MARK: Weight, time, flow

    /// Grams to one decimal: "36.0".
    static func grams(_ value: Double) -> String { decimal(value, 1) }
    /// Grams to one decimal with the unit: "36.0 g".
    static func gramsUnit(_ value: Double) -> String { grams(value) + " g" }
    /// Seconds to one decimal: "27.5".
    static func seconds(_ value: Double) -> String { decimal(value, 1) }
    /// Seconds to one decimal with the unit: "27.5 s".
    static func secondsUnit(_ value: Double) -> String { seconds(value) + " s" }
    /// Flow to two decimals: "1.25".
    static func flow(_ value: Double) -> String { decimal(value, 2) }
    /// Flow to two decimals with the unit: "1.25 g/s".
    static func flowUnit(_ value: Double) -> String { flow(value) + " g/s" }
    /// Ratio as the board writes it: "1:2.0".
    static func ratio(_ value: Double) -> String { "1:" + decimal(value, 1) }
    /// A shot clock "27.5" under a minute, "1:05.0" above.
    static func clock(_ value: Double) -> String {
        guard value >= 60 else { return seconds(max(0, value)) }
        let minutes = Int(value) / 60
        let rest = value - Double(minutes * 60)
        return String(format: "%d:%04.1f", minutes, rest)
    }
    /// A battery percentage: "85%", or "--" when not reported (negative).
    static func battery(_ percent: Int) -> String { percent < 0 ? "--" : "\(percent)%" }
    /// Temperature to one decimal: "21.5".
    static func celsius(_ value: Double) -> String { decimal(value, 1) + " °C" }

    /// A fixed-decimal number with a dot, whatever the phone's region (the board always uses a dot).
    static func decimal(_ value: Double, _ places: Int) -> String {
        guard value.isFinite else { return "--" }
        let text = String(format: "%.\(places)f", locale: Locale(identifier: "en_US_POSIX"), value)
        return text == "-0.0" || text == "-0.00" ? String(text.dropFirst()) : text
    }

    // MARK: Grind

    /// How a grinder's setting is written down (core/SettingFormat.h): plain whole steps, a dial read in
    /// fractions (divisor 2, 4, 5, 10, ...), or two rings (`subSteps` inner steps per outer step).
    struct Dial: Sendable, Equatable {
        var unit: String
        var divisor: Int
        var subSteps: Int

        init(unit: String = "um", divisor: Int = 1, subSteps: Int = 0) {
            self.unit = unit
            self.divisor = divisor < 1 ? 1 : divisor
            self.subSteps = subSteps >= 2 ? subSteps : 0
        }

        static let microns = Dial()

        var isRinged: Bool { subSteps >= 2 }

        private var decimals: Int { divisor <= 1 ? 0 : ((divisor == 2 || divisor == 5 || divisor == 10) ? 1 : 2) }

        /// The stored setting as the dial reads, without the unit. "" for 0 (not entered).
        func setting(_ stored: Int) -> String {
            guard stored > 0 else { return "" }
            if isRinged {
                return "\((stored - 1) / subSteps + 1):\((stored - 1) % subSteps + 1)"
            }
            if divisor > 1 {
                let scale = decimals == 1 ? 10 : 100
                let rest = (stored % divisor) * (scale / divisor)
                return "\(stored / divisor)." + String(format: "%0\(decimals)ld", rest)
            }
            return String(stored)
        }

        /// The setting with its unit: "246 um", "3.5 clicks", "12:3".
        func settingUnit(_ stored: Int) -> String {
            guard stored > 0 else { return "--" }
            return isRinged ? setting(stored) : "\(setting(stored)) \(unit)"
        }

        /// A change of setting without the unit; on a ringed dial the count is inner steps.
        func delta(_ steps: Int) -> String {
            let a = abs(steps)
            if !isRinged && divisor > 1 {
                let scale = decimals == 1 ? 10 : 100
                return "\(a / divisor)." + String(format: "%0\(decimals)ld", (a % divisor) * (scale / divisor))
            }
            return String(a)
        }

        /// What to call a change of setting: the unit, or "steps" on a ringed dial.
        var deltaUnit: String { isRinged ? "steps" : unit }

        /// A signed change with its unit: "+5 um", "-1.5 clicks".
        func signedDelta(_ steps: Int) -> String {
            (steps < 0 ? "-" : "+") + delta(steps) + " " + deltaUnit
        }
    }

    // MARK: Dates

    /// YYYYMMDD (how the controller stores dates) to a date, nil when 0 or invalid.
    static func date(fromYMD ymd: Int?) -> Date? {
        guard let ymd, ymd > 19_000_000 else { return nil }
        var parts = DateComponents()
        parts.year = ymd / 10_000
        parts.month = (ymd / 100) % 100
        parts.day = ymd % 100
        return Calendar(identifier: .gregorian).date(from: parts)
    }

    /// A Unix time in seconds to a date, nil when 0 (not set).
    static func date(fromEpoch seconds: Int?) -> Date? {
        guard let seconds, seconds > 0 else { return nil }
        return Date(timeIntervalSince1970: TimeInterval(seconds))
    }
}
