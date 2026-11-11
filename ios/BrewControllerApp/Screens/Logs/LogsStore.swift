// What the Logs screen shows and does: the level, the live tail (GET /api/log?from=N, following the
// X-Log-Next header), the download and the clear.
import Foundation
import Observation

@MainActor
@Observable
final class LogsStore {
    var config = LogsConfig()
    /// The tail as one text, newest last (at most `maxChars`).
    var text = ""
    var error: String?
    var notice: String?

    static let maxChars = 200_000

    @ObservationIgnored private var next = 0
    @ObservationIgnored private var polling = false
    private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    func loadConfig() async {
        do {
            config = try await api.get("/api/log/config")
        } catch let failure as BrewAPIError {
            error = failure.message
        } catch {
            self.error = String(describing: error)
        }
    }

    /// Reads what is new, up to 8 chunks; a position that went backwards means the log restarted.
    func poll() async {
        guard !polling else { return }
        polling = true
        defer { polling = false }
        for _ in 0..<8 {
            guard let response = try? await api.getRaw("/api/log", query: ["from": String(next)]) else { return }
            let chunk = String(decoding: response.body, as: UTF8.self)
            let position = Int(response.header("X-Log-Next") ?? "") ?? next
            if position < next { text = "" }
            next = position
            if chunk.isEmpty { break }
            text += chunk
            if text.count > Self.maxChars { text = String(text.suffix(Self.maxChars)) }
        }
    }

    func clearView() { text = "" }

    func setLevel(_ level: Int) async {
        await perform("Saved") {
            try await api.post("/api/log/config", json: ["level": .int(level), "keep": .bool(config.keep)])
        }
    }

    func setKeep(_ keep: Bool) async {
        await perform("Saved") {
            try await api.post("/api/log/config", json: ["level": .int(config.level), "keep": .bool(keep)])
        }
    }

    func clearStored() async {
        await perform("Log cleared") {
            try await api.post("/api/log/clear")
        }
        text = ""
        next = 0
    }

    /// The whole log as a file in the temporary folder, for the share sheet.
    func downloadFile() async -> URL? {
        error = nil
        do {
            let file = try await api.downloadLog()
            let url = FileManager.default.temporaryDirectory.appendingPathComponent(file.filename)
            try file.data.write(to: url, options: .atomic)
            return url
        } catch let failure as BrewAPIError {
            error = failure.message
        } catch {
            self.error = String(describing: error)
        }
        return nil
    }

    private func perform(_ done: String?, _ work: () async throws -> Void) async {
        error = nil
        notice = nil
        do {
            try await work()
            if let done { notice = done }
            await loadConfig()
        } catch let failure as BrewAPIError {
            error = failure.message
        } catch {
            self.error = String(describing: error)
        }
    }
}
