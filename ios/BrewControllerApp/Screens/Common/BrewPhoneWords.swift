// The board's words that name a key or a gear the phone does not have, said the phone's way. Everything
// else is the firmware's own text (ScreenText.h through /api/screen).
import Foundation

enum BrewPhoneWords {
    /// The board's "touch the key to brew" (its hint carries an arrow the app draws as a Start key).
    static let touchHint = "TAP START TO BREW"

    private static let replacements: [(board: String, phone: String)] = [
        ("TOUCH KEY\nTO BREW", "TAP START\nTO BREW"),
        ("TOUCH KEY TO BREW", "TAP START TO BREW"),
        ("PAIR THE BOT IN SETTINGS", "PAIR THE BOT: TAP SETTINGS"),
        ("Bluetooth failed to start. Restart the controller.", "Bluetooth failed to start. Close the app and open it again."),
        ("Restart the controller.", "Close the app and open it again."),
    ]

    /// A phase, hint or fault text of the firmware in the phone's words.
    static func phone(_ text: String) -> String {
        var out = text
        for pair in replacements where out.contains(pair.board) {
            out = out.replacingOccurrences(of: pair.board, with: pair.phone)
        }
        return out
    }
}
