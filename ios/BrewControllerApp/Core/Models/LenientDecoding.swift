// Lenient decoding for the controller's JSON: unknown fields are ignored (Codable does that), a missing
// or oddly typed field falls back to a default or nil, so later firmware additions never break the app.
import Foundation

extension KeyedDecodingContainer {
    /// The value for `key`, or `fallback` when the key is missing, null or of another type.
    func lenient<T: Decodable>(_ key: Key, _ fallback: T) -> T {
        (try? decodeIfPresent(T.self, forKey: key)) ?? fallback
    }

    /// The value for `key`, or nil when the key is missing, null or of another type.
    func lenient<T: Decodable>(_ key: Key) -> T? {
        (try? decodeIfPresent(T.self, forKey: key)) ?? nil
    }
}

/// A JSON number that may be `null` (the controller writes NaN series points as null).
typealias OptionalSeries = [Double?]
