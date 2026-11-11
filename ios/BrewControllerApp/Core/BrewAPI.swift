// BrewAPI: async get/post over the in-process web API (bbw_request, ios/Host/BrewHost.h).
// Every call runs on one dedicated serial background queue, never the main thread; the controller
// answers one request at a time on its loop thread anyway.
import Foundation

// MARK: - Errors

/// What can go wrong with a request, typed so a screen can word it.
enum BrewAPIError: Error, Sendable, Equatable {
    /// The controller is not running (yet): bbw_request returned nonzero.
    case notReady
    /// The controller did not answer in time (HTTP 503 from bbw_request).
    case timeout
    /// HTTP status 400 or more, with the `error` text of `{"ok":false,"error":...}` when there was one.
    case http(status: Int, message: String?)
    /// Status 200 but `{"ok":false,"error":...}`.
    case rejected(message: String)
    /// The answer was not the JSON the model expected.
    case decoding(String)
    /// The request could not be built (a path or body that cannot be encoded).
    case invalidRequest(String)

    /// The words for a screen.
    var message: String {
        switch self {
        case .notReady: return "The controller is not ready yet."
        case .timeout: return "The controller did not answer in time."
        case .http(let status, let message): return message ?? "Request failed (\(status))."
        case .rejected(let message): return message
        case .decoding(let detail): return "Unexpected answer: \(detail)"
        case .invalidRequest(let detail): return detail
        }
    }

    /// HTTP 409: "not while a shot is running" and similar.
    var isConflict: Bool {
        if case .http(let status, _) = self { return status == 409 }
        return false
    }
}

// MARK: - Raw response

/// What the controller answered, copied out of the C buffer.
struct BrewRawResponse: Sendable {
    var status: Int
    var contentType: String
    var headers: [String: String]
    var body: Data

    var isSuccess: Bool { status >= 200 && status < 300 }

    /// The `error` text of a JSON error body, if the body has one.
    var errorMessage: String? {
        guard let object = try? JSONSerialization.jsonObject(with: body) as? [String: Any] else { return nil }
        return object["error"] as? String
    }

    /// Case-insensitive header lookup.
    func header(_ name: String) -> String? {
        let lower = name.lowercased()
        for (key, value) in headers where key.lowercased() == lower { return value }
        return nil
    }
}

/// A file the controller sent (backup, CSV, log): the bytes and the name from Content-Disposition.
struct BrewDownload: Sendable {
    var data: Data
    var filename: String
    var contentType: String
}

/// A JSON value for request bodies built by hand.
enum BrewJSON: Sendable, Equatable {
    case null
    case bool(Bool)
    case int(Int)
    case double(Double)
    case string(String)
    case array([BrewJSON])
    case object([String: BrewJSON])

    fileprivate var foundation: Any {
        switch self {
        case .null: return NSNull()
        case .bool(let v): return v
        case .int(let v): return v
        case .double(let v): return v
        case .string(let v): return v
        case .array(let v): return v.map { $0.foundation }
        case .object(let v): return v.mapValues { $0.foundation }
        }
    }
}

extension BrewJSON: ExpressibleByStringLiteral, ExpressibleByBooleanLiteral, ExpressibleByIntegerLiteral,
    ExpressibleByFloatLiteral {
    init(stringLiteral value: String) { self = .string(value) }
    init(booleanLiteral value: Bool) { self = .bool(value) }
    init(integerLiteral value: Int) { self = .int(value) }
    init(floatLiteral value: Double) { self = .double(value) }
}

// MARK: - BrewAPI

final class BrewAPI: Sendable {
    /// One shared instance: the app has one controller.
    static let shared = BrewAPI()

    static let defaultTimeoutMs = 15_000

    private let queue = DispatchQueue(label: "bbw.api", qos: .userInitiated)

    init() {}

    // MARK: Low level

    /// One request, on the background queue. Throws only `.notReady` or `.invalidRequest`;
    /// every HTTP status (a 503 timeout included) comes back in the response.
    func raw(_ method: String, _ path: String, body: Data? = nil, contentType: String? = nil,
             timeoutMs: Int = BrewAPI.defaultTimeoutMs) async throws -> BrewRawResponse {
        guard !path.isEmpty, path.hasPrefix("/") else { throw BrewAPIError.invalidRequest("Bad path: \(path)") }
        let result: BrewRawResponse? = await withCheckedContinuation { continuation in
            queue.async {
                continuation.resume(returning: BrewAPI.perform(method, path, body, contentType, timeoutMs))
            }
        }
        guard let result else { throw BrewAPIError.notReady }
        return result
    }

    /// Runs bbw_request. nil: the host is not running.
    private static func perform(_ method: String, _ path: String, _ body: Data?, _ contentType: String?,
                                _ timeoutMs: Int) -> BrewRawResponse? {
        var response = BbwResponse()
        let code: Int32
        if let body, !body.isEmpty {
            code = body.withUnsafeBytes { buffer -> Int32 in
                withOptionalCString(contentType) { type in
                    bbw_request(method, path, buffer.baseAddress, buffer.count, type, Int32(timeoutMs), &response)
                }
            }
        } else {
            code = withOptionalCString(contentType) { type in
                bbw_request(method, path, nil, 0, type, Int32(timeoutMs), &response)
            }
        }
        guard code == 0 else { return nil }
        defer { bbw_response_free(&response) }

        let data: Data
        if let bodyPointer = response.body, response.body_len > 0 {
            data = Data(bytes: bodyPointer, count: Int(response.body_len))
        } else {
            data = Data()
        }
        let type = response.content_type.map { String(cString: $0) } ?? ""
        let headerText = response.headers.map { String(cString: $0) } ?? ""
        return BrewRawResponse(status: Int(response.status), contentType: type,
                               headers: parseHeaders(headerText), body: data)
    }

    // MARK: Checked

    /// A response that must be a success: HTTP errors and `{"ok":false}` become typed errors.
    func checked(_ response: BrewRawResponse) throws -> BrewRawResponse {
        if response.status == 503 && response.errorMessage == nil && response.body.isEmpty { throw BrewAPIError.timeout }
        if response.status >= 400 {
            if response.status == 503 && response.contentType.isEmpty { throw BrewAPIError.timeout }
            throw BrewAPIError.http(status: response.status, message: response.errorMessage)
        }
        if response.contentType.contains("json"), let object = try? JSONSerialization.jsonObject(with: response.body)
            as? [String: Any], let ok = object["ok"] as? Bool, ok == false {
            throw BrewAPIError.rejected(message: (object["error"] as? String) ?? "The controller said no.")
        }
        return response
    }

    // MARK: GET / POST

    /// GET path with an optional query, decoded as `T`.
    func get<T: Decodable & Sendable>(_ path: String, query: [String: String] = [:], as type: T.Type = T.self,
                                      timeoutMs: Int = BrewAPI.defaultTimeoutMs) async throws -> T {
        let response = try checked(await raw("GET", BrewAPI.pathWithQuery(path, query), timeoutMs: timeoutMs))
        return try BrewAPI.decode(type, from: response.body)
    }

    /// GET path, the checked raw answer (for routes whose body is not one model).
    func getRaw(_ path: String, query: [String: String] = [:],
                timeoutMs: Int = BrewAPI.defaultTimeoutMs) async throws -> BrewRawResponse {
        try checked(await raw("GET", BrewAPI.pathWithQuery(path, query), timeoutMs: timeoutMs))
    }

    /// POST a JSON body; the answer is thrown away (errors still throw). Returns the raw answer.
    @discardableResult
    func post(_ path: String, json: [String: BrewJSON] = [:], query: [String: String] = [:],
              timeoutMs: Int = BrewAPI.defaultTimeoutMs) async throws -> BrewRawResponse {
        let data = try BrewAPI.encode(json)
        return try checked(await raw("POST", BrewAPI.pathWithQuery(path, query), body: data,
                                     contentType: "application/json", timeoutMs: timeoutMs))
    }

    /// POST a JSON body and decode the answer as `T`.
    func post<T: Decodable & Sendable>(_ path: String, json: [String: BrewJSON] = [:], query: [String: String] = [:],
                                       as type: T.Type, timeoutMs: Int = BrewAPI.defaultTimeoutMs) async throws -> T {
        let response = try await post(path, json: json, query: query, timeoutMs: timeoutMs)
        return try BrewAPI.decode(type, from: response.body)
    }

    /// POST an `application/x-www-form-urlencoded` body (what the web page sends to some routes).
    @discardableResult
    func postForm(_ path: String, fields: [String: String],
                  timeoutMs: Int = BrewAPI.defaultTimeoutMs) async throws -> BrewRawResponse {
        let body = BrewAPI.formEncoded(fields).data(using: .utf8) ?? Data()
        return try checked(await raw("POST", path, body: body, contentType: "application/x-www-form-urlencoded",
                                     timeoutMs: timeoutMs))
    }

    /// POST /api/screen/do?what=...: the board's screen actions.
    @discardableResult
    func screenAction(_ what: String, _ args: [String: String] = [:]) async throws -> BrewRawResponse {
        var query = args
        query["what"] = what
        return try checked(await raw("POST", BrewAPI.pathWithQuery("/api/screen/do", query)))
    }

    // MARK: Files

    /// GET a file (backup, CSV, log): the bytes and the name from Content-Disposition (`fallbackName` when
    /// the answer has none).
    func download(_ path: String, query: [String: String] = [:], fallbackName: String = "download",
                  timeoutMs: Int = 60_000) async throws -> BrewDownload {
        let response = try checked(await raw("GET", BrewAPI.pathWithQuery(path, query), timeoutMs: timeoutMs))
        let name = BrewAPI.filename(fromContentDisposition: response.header("Content-Disposition")) ?? fallbackName
        return BrewDownload(data: response.body, filename: name, contentType: response.contentType)
    }

    /// POST /api/backup... is a GET: the whole backup file.
    func downloadBackup() async throws -> BrewDownload {
        try await download("/api/backup", fallbackName: "brew-controller-backup.bin", timeoutMs: 120_000)
    }

    /// All shots as CSV.
    func downloadShotsCSV() async throws -> BrewDownload {
        try await download("/shots.csv", fallbackName: "shots.csv")
    }

    /// One shot as CSV.
    func downloadShotCSV(id: UInt32) async throws -> BrewDownload {
        try await download("/shot.csv", query: ["id": String(id)], fallbackName: "shot-\(id).csv")
    }

    /// The controller's log.
    func downloadLog() async throws -> BrewDownload {
        try await download("/log.txt", fallbackName: "brew-controller-log.txt")
    }

    /// One file as a one-part multipart body to `path` (the restore: `/api/restore`). The answer is the
    /// raw checked response; decode it with `BrewAPI.decode` (e.g. `RestoreSummary`).
    @discardableResult
    func upload(_ path: String, fileData: Data, fileName: String, fieldName: String = "file",
                mimeType: String = "application/octet-stream",
                timeoutMs: Int = 120_000) async throws -> BrewRawResponse {
        let boundary = "BrewBoundary" + UUID().uuidString.replacingOccurrences(of: "-", with: "")
        var body = Data()
        func add(_ text: String) { body.append(Data(text.utf8)) }
        add("--\(boundary)\r\n")
        add("Content-Disposition: form-data; name=\"\(fieldName)\"; filename=\"\(fileName)\"\r\n")
        add("Content-Type: \(mimeType)\r\n\r\n")
        body.append(fileData)
        add("\r\n--\(boundary)--\r\n")
        return try checked(await raw("POST", path, body: body,
                                     contentType: "multipart/form-data; boundary=\(boundary)",
                                     timeoutMs: timeoutMs))
    }

    // MARK: Helpers (static, pure)

    static func decode<T: Decodable>(_ type: T.Type, from data: Data) throws -> T {
        do {
            return try JSONDecoder().decode(type, from: data)
        } catch {
            throw BrewAPIError.decoding(String(describing: error))
        }
    }

    static func encode(_ json: [String: BrewJSON]) throws -> Data {
        let object = json.mapValues { $0.foundation }
        do {
            return try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        } catch {
            throw BrewAPIError.invalidRequest("Cannot encode the request body.")
        }
    }

    static func pathWithQuery(_ path: String, _ query: [String: String]) -> String {
        guard !query.isEmpty else { return path }
        let pairs = query.sorted { $0.key < $1.key }.map { "\(percentEncode($0.key))=\(percentEncode($0.value))" }
        return path + (path.contains("?") ? "&" : "?") + pairs.joined(separator: "&")
    }

    static func formEncoded(_ fields: [String: String]) -> String {
        fields.sorted { $0.key < $1.key }.map { "\(percentEncode($0.key))=\(percentEncode($0.value))" }
            .joined(separator: "&")
    }

    private static let unreserved = CharacterSet(
        charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~")

    static func percentEncode(_ text: String) -> String {
        text.addingPercentEncoding(withAllowedCharacters: unreserved) ?? text
    }

    /// The file name of `attachment; filename="x.csv"` (also `filename*=UTF-8''...`).
    static func filename(fromContentDisposition value: String?) -> String? {
        guard let value else { return nil }
        for part in value.split(separator: ";") {
            let piece = part.trimmingCharacters(in: .whitespaces)
            if piece.lowercased().hasPrefix("filename*=") {
                var text = String(piece.dropFirst("filename*=".count))
                if let marker = text.range(of: "''") { text = String(text[marker.upperBound...]) }
                if let decoded = text.removingPercentEncoding, !decoded.isEmpty { return decoded }
            }
        }
        for part in value.split(separator: ";") {
            let piece = part.trimmingCharacters(in: .whitespaces)
            if piece.lowercased().hasPrefix("filename=") {
                let text = String(piece.dropFirst("filename=".count)).trimmingCharacters(in: CharacterSet(charactersIn: "\""))
                if !text.isEmpty { return text }
            }
        }
        return nil
    }

    /// "Name: value" lines as a dictionary.
    static func parseHeaders(_ text: String) -> [String: String] {
        var result: [String: String] = [:]
        for line in text.split(whereSeparator: { $0 == "\n" || $0 == "\r" }) {
            guard let colon = line.firstIndex(of: ":") else { continue }
            let name = line[..<colon].trimmingCharacters(in: .whitespaces)
            let value = line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces)
            if !name.isEmpty { result[name] = value }
        }
        return result
    }
}


/// Calls `body` with a C string, or nil when `string` is nil.
private func withOptionalCString<R>(_ string: String?, _ body: (UnsafePointer<CChar>?) -> R) -> R {
    if let string { return string.withCString { body($0) } }
    return body(nil)
}
