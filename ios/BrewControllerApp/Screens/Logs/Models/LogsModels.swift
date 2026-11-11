// GET /api/log/config (Web.cpp handleLogConfig).
import Foundation

struct LogsConfig: Codable, Sendable, Equatable {
    /// 0 errors only, 1 warnings and errors, 2 normal, 3 debug.
    var level: Int = 2
    var keep = true
    var flashBytes: Int = 0
    var flashLimit: Int = 0
    var memoryBytes: Int = 0
    var position: Int = 0

    enum CodingKeys: String, CodingKey {
        case level, keep, position
        case flashBytes = "flash_bytes", flashLimit = "flash_limit", memoryBytes = "memory_bytes"
    }
}

extension LogsConfig {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        level = c.lenient(.level, 2)
        keep = c.lenient(.keep, true)
        flashBytes = c.lenient(.flashBytes, 0)
        flashLimit = c.lenient(.flashLimit, 0)
        memoryBytes = c.lenient(.memoryBytes, 0)
        position = c.lenient(.position, 0)
    }
}

/// The detail levels, in the board's words.
enum LogsLevel {
    static let names = [
        0: "Errors only",
        1: "Warnings and errors",
        2: "Normal: every event",
        3: "Debug: also once a second during a shot",
    ]
}
