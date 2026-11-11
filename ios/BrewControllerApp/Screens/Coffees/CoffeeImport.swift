// Coffees from elsewhere: a Beanconqueror backup read from a file, a shot handed to Beanconqueror as a
// beanconqueror:// link, and a lookup on visualizer.coffee. These are ports of WebPage.h `bcBeanToCoffee`,
// `bcEnvelope`, `bcLink`, `vzToCoffee` and `vzSearch`; the names of the fields are Beanconqueror's and
// visualizer.coffee's, not ours.
import Foundation

// MARK: - A coffee as the form edits it

/// The fields of the coffee form (WebPage.h `CF`). Dates are YYYYMMDD, 0 not set.
struct CoffeeDraft: Equatable, Sendable {
    var name = ""
    var roaster = ""
    var roastLevel = 0
    var notes = ""
    var roastDate = 0
    var frozenDate = 0
    var thawDate = 0
    var origin = ""
    var region = ""
    var producer = ""
    var varietal = ""
    var process = ""
    var altitudeM = 0

    init() {}

    init(_ coffee: CoffeeModel) {
        name = coffee.name
        roaster = coffee.roaster
        roastLevel = coffee.roastLevel
        notes = coffee.notes
        roastDate = coffee.roastDate
        frozenDate = coffee.frozenDate
        thawDate = coffee.thawDate
        origin = coffee.origin
        region = coffee.region
        producer = coffee.producer
        varietal = coffee.varietal
        process = coffee.process
        altitudeM = coffee.altitudeM
    }

    init(_ entry: CoffeeLibraryEntry) {
        name = entry.name
        roaster = entry.roaster
        roastLevel = entry.roastLevelNumber
        notes = entry.notes
        origin = entry.origin
        region = entry.region
        producer = entry.producer
        varietal = entry.varietal
        process = entry.process
        altitudeM = entry.altitudeM
    }

    /// Limits of the controller's fields (Web.cpp `handleSaveCoffee`, WebPage.h maxlength attributes).
    func clipped() -> CoffeeDraft {
        var copy = self
        copy.name = Self.clip(name, 23)
        copy.roaster = Self.clip(roaster, 23)
        copy.notes = Self.clip(notes, 47)
        copy.origin = Self.clip(origin, 23)
        copy.region = Self.clip(region, 31)
        copy.producer = Self.clip(producer, 31)
        copy.varietal = Self.clip(varietal, 31)
        copy.process = Self.clip(process, 15)
        copy.altitudeM = min(max(altitudeM, 0), 4000)
        copy.roastLevel = min(max(roastLevel, 0), 5)
        return copy
    }

    /// Text without quotes and line breaks, cut to the controller's field size.
    static func clip(_ text: String, _ limit: Int) -> String {
        let cleaned = text.components(separatedBy: CharacterSet(charactersIn: "\"\r\n")).joined(separator: " ")
        return String(cleaned.trimmingCharacters(in: .whitespaces).prefix(limit))
    }

    /// The body of POST /api/coffee (and /api/coffee/adopt, with a libraryId added by the caller).
    func body(id: Int) -> [String: BrewJSON] {
        let c = clipped()
        return [
            "id": .int(id),
            "name": .string(c.name),
            "roaster": .string(c.roaster),
            "origin": .string(c.origin),
            "region": .string(c.region),
            "producer": .string(c.producer),
            "varietal": .string(c.varietal),
            "process": .string(c.process),
            "notes": .string(c.notes),
            "altitudeM": .int(c.altitudeM),
            "roastLevel": .int(c.roastLevel),
            "roastDate": .int(c.roastDate),
            "frozenDate": .int(c.frozenDate),
            "thawDate": .int(c.thawDate),
        ]
    }
}

// MARK: - Beanconqueror

enum CoffeeBeanconqueror {
    /// One bean of a backup, as a coffee and the bag it comes with.
    struct Bean: Equatable, Sendable {
        var coffee: CoffeeDraft
        var openedDate: Int
        var frozenDate: Int
        var thawDate: Int
        var weightG: Int
        var finished: Bool

        var hasBagDetails: Bool { openedDate != 0 || weightG != 0 || frozenDate != 0 }
    }

    struct ParseResult: Equatable, Sendable {
        var beans: [Bean] = []
        var skipped: [String] = []
        var total = 0
    }

    /// Beanconqueror's 14 roast names to the controller's 5 levels (a judgement, not a standard).
    private static let roastLevels: [String: Int] = [
        "CINNAMON_ROAST": 1, "NEW_ENGLAND_ROAST": 1, "HALF_CITY_ROAST": 1, "MODERATE_LIGHT_ROAST": 2,
        "AMERICAN_ROAST": 2, "CITY_ROAST": 3, "CITY_PLUS_ROAST": 3, "FULL_CITY_ROAST": 4,
        "FULL_CITY_PLUS_ROAST": 4, "VIEANNA_ROAST": 5, "VIENNA_ROAST": 5, "ITALIAN_ROAST": 5, "FRENCH_ROAST": 5,
    ]

    /// The beans in a backup file. nil when the file is not JSON.
    static func parse(_ data: Data, includeFinished: Bool) -> ParseResult? {
        guard let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return nil }
        let list: [[String: Any]]
        if let beans = root["BEANS"] as? [[String: Any]] {
            list = beans
        } else if let bean = root["bean"] as? [String: Any] {
            list = [bean]
        } else {
            list = []
        }
        var result = ParseResult()
        result.total = list.count
        for entry in list {
            let bean = self.bean(entry)
            if bean.coffee.name.isEmpty {
                result.skipped.append("a bean with no name")
            } else if bean.finished && !includeFinished {
                result.skipped.append(bean.coffee.name + " (finished)")
            } else {
                result.beans.append(bean)
            }
        }
        return result
    }

    static func bean(_ b: [String: Any]) -> Bean {
        let info = (b["bean_information"] as? [[String: Any]])?.first ?? [:]
        var coffee = CoffeeDraft()
        coffee.name = CoffeeDraft.clip(text(b["name"]), 23)
        coffee.roaster = CoffeeDraft.clip(text(b["roaster"]), 23)
        coffee.origin = CoffeeDraft.clip(text(info["country"]), 23)
        coffee.region = CoffeeDraft.clip(text(info["region"]), 31)
        let farmer = text(info["farmer"])
        coffee.producer = CoffeeDraft.clip(farmer.isEmpty ? text(info["farm"]) : farmer, 31)
        coffee.varietal = CoffeeDraft.clip(text(info["variety"]), 31)
        coffee.process = CoffeeDraft.clip(text(info["processing"]), 15)
        coffee.notes = CoffeeDraft.clip(text(b["aromatics"]), 47)
        coffee.altitudeM = altitude(text(info["elevation"]))
        coffee.roastLevel = roastLevels[text(b["roast"])] ?? 0
        coffee.roastDate = ymd(text(b["roastingDate"]))
        let weight = (b["weight"] as? NSNumber)?.doubleValue ?? 0
        return Bean(coffee: coffee, openedDate: ymd(text(b["openDate"])), frozenDate: ymd(text(b["frozenDate"])),
                    thawDate: ymd(text(b["unfrozenDate"])), weightG: weight > 0 && weight < 65_535 ? Int(weight.rounded()) : 0,
                    finished: (b["finished"] as? Bool) ?? false)
    }

    /// A JSON value as text: strings as they are, numbers written out, anything else empty.
    static func text(_ value: Any?) -> String {
        if let string = value as? String { return string }
        if let number = value as? NSNumber { return number.stringValue }
        return ""
    }

    /// "2026-11-01T00:00:00.000Z" to 20261101; 0 when it does not start with a date.
    static func ymd(_ iso: String) -> Int {
        let parts = iso.prefix(10).split(separator: "-")
        guard parts.count == 3, parts[0].count == 4, let y = Int(parts[0]), let m = Int(parts[1]), let d = Int(parts[2])
        else { return 0 }
        return y * 10_000 + m * 100 + d
    }

    /// The first number of "1,800 masl" or "1800-2000 m" as metres; 0 when out of range.
    static func altitude(_ text: String) -> Int {
        guard let range = text.range(of: #"\d[\d.,]*"#, options: .regularExpression) else { return 0 }
        let digits = text[range].filter { $0 != "." && $0 != "," }
        guard let value = Int(digits), value >= 1, value <= 4000 else { return 0 }
        return value
    }

    // MARK: Export: a shot as Beanconqueror's brew hand-off link

    /// `beanconqueror://ADD_BREW?...` for one shot: its envelope as JSON, gzip, unpadded base64url, cut into
    /// query chunks of at most 400 characters. `now` dates a shot whose clock was not set.
    static func link(shot: ShotModel, data: ShotDataModel?, coffee: CoffeeModel?, grinderModel: String,
                     grindText: String, now: Date?) -> URL? {
        let envelope = self.envelope(shot: shot, data: data, coffee: coffee, grinderModel: grinderModel,
                                     grindText: grindText, now: now)
        guard JSONSerialization.isValidJSONObject(envelope),
              let json = try? JSONSerialization.data(withJSONObject: envelope),
              let zipped = gzip(json) else { return nil }
        let payload = base64URL(zipped)
        var query = "len=\(payload.count)"
        var index = 0
        var start = payload.startIndex
        while start < payload.endIndex {
            let end = payload.index(start, offsetBy: 400, limitedBy: payload.endIndex) ?? payload.endIndex
            query += "&shareBrew\(index)=" + payload[start..<end]
            index += 1
            start = end
        }
        return URL(string: "beanconqueror://ADD_BREW?" + query)
    }

    static func envelope(shot: ShotModel, data: ShotDataModel?, coffee: CoffeeModel?, grinderModel: String,
                         grindText: String, now: Date?) -> [String: Any] {
        let date = shot.timestamp.isEmpty || shot.date == nil
            ? ISO8601DateFormatter().string(from: now ?? Date()) : shot.timestamp
        var note: [String] = []
        if !shot.taste.isEmpty { note.append("Tasted \(shot.taste).") }
        if let tds = shot.tdsPct, tds > 0 {
            var text = "TDS \(BrewFormat.decimal(tds, 2)) %"
            if let extraction = shot.extractionPct, extraction > 0 {
                text += ", extraction \(BrewFormat.decimal(extraction, 2)) %"
            }
            note.append(text + ".")
        }
        if !shot.endReason.isEmpty { note.append("Ended by \(shot.endReason.replacingOccurrences(of: "_", with: " ")).") }

        var brew: [String: Any] = [
            "date": date,
            "waterIn": quantity(shot.finalWeightG, "ml"),
            "beverageOut": quantity(shot.finalWeightG, "g"),
            "brewTime": shot.shotTimeS,
            "preparationMethod": "Espresso",
        ]
        if shot.doseG > 0 { brew["doseIn"] = quantity(shot.doseG, "g") }
        if shot.firstDropS > 0 { brew["firstDripTime"] = shot.firstDropS }
        if !grindText.isEmpty { brew["grindSize"] = grindText }
        if !grinderModel.isEmpty { brew["grinderName"] = grinderModel }
        if shot.rating >= 1 && shot.rating <= 5 { brew["rating"] = shot.rating }
        if !note.isEmpty { brew["note"] = note.joined(separator: " ") }

        var envelope: [String: Any] = [
            "v": 1,
            "app": ["name": "Brew by Weight controller", "version": "1"] as [String: Any],
            "brew": brew,
            "imported": [
                "source": "bbw-controller", "sourceName": "Brew by Weight controller", "schema": 1,
                "params": ["shotId": shot.id, "endReason": shot.endReason,
                           "controllerTargetG": shot.stopTargetG > 0 ? shot.stopTargetG : shot.targetWeightG] as [String: Any],
            ] as [String: Any],
        ]

        // The coffee: the shelf's, or what the shot itself recorded.
        var bean: [String: Any] = [:]
        if let coffee, !coffee.name.isEmpty {
            bean["name"] = coffee.name
            if !coffee.roaster.isEmpty { bean["roaster"] = coffee.roaster }
            if !coffee.origin.isEmpty { bean["origin"] = coffee.origin }
            if !coffee.region.isEmpty { bean["region"] = coffee.region }
            if !coffee.producer.isEmpty { bean["farmer"] = coffee.producer }
            if !coffee.process.isEmpty { bean["process"] = coffee.process }
            if !coffee.varietal.isEmpty { bean["variety"] = coffee.varietal }
            if coffee.altitudeM > 0 { bean["elevation"] = String(coffee.altitudeM) }
            if !coffee.notes.isEmpty { bean["aromatics"] = coffee.notes }
            if coffee.roastDate > 0 { bean["roastingDate"] = isoDate(coffee.roastDate) }
        } else if !shot.coffee.isEmpty {
            bean["name"] = shot.coffee
            if !shot.region.isEmpty { bean["region"] = shot.region }
            if !shot.varietal.isEmpty { bean["variety"] = shot.varietal }
            if shot.roastDate > 0 { bean["roastingDate"] = isoDate(shot.roastDate) }
        }
        if !bean.isEmpty { envelope["bean"] = bean }

        if let data, data.timeS.count > 1, data.weightG.count == data.timeS.count {
            var times: [Int] = []
            var weights: [Double] = []
            var previousT = 0.0
            var previousW = 0.0
            for index in data.timeS.indices {
                times.append(max(0, Int(((data.timeS[index] - previousT) * 1000).rounded())))
                previousT = data.timeS[index]
                weights.append(((data.weightG[index] - previousW) * 10 * 100).rounded() / 100)
                previousW = data.weightG[index]
            }
            envelope["flow"] = ["fidelity": "full", "t": times, "waterDispensed": times.map { _ in 0 },
                                "weight": weights] as [String: Any]
        }
        return envelope
    }

    private static func quantity(_ value: Double, _ unit: String) -> [String: Any] {
        ["value": value, "unit": unit]
    }

    /// 20261101 to "2026-11-01".
    static func isoDate(_ ymd: Int) -> String {
        String(format: "%04d-%02d-%02d", ymd / 10_000, (ymd / 100) % 100, ymd % 100)
    }

    // MARK: gzip and base64url

    /// RFC 1952: a 10-byte header, the raw deflate stream (what Apple's `.zlib` algorithm writes), the CRC-32
    /// and the length of the input.
    static func gzip(_ data: Data) -> Data? {
        guard let deflated = try? (data as NSData).compressed(using: .zlib) as Data else { return nil }
        var out = Data([0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF])
        out.append(deflated)
        var crc = crc32(data).littleEndian
        withUnsafeBytes(of: &crc) { out.append(contentsOf: $0) }
        var size = UInt32(truncatingIfNeeded: data.count).littleEndian
        withUnsafeBytes(of: &size) { out.append(contentsOf: $0) }
        return out
    }

    private static let crcTable: [UInt32] = (0..<256).map { index -> UInt32 in
        var value = UInt32(index)
        for _ in 0..<8 { value = value & 1 == 1 ? (0xEDB8_8320 ^ (value >> 1)) : (value >> 1) }
        return value
    }

    static func crc32(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in data { crc = crcTable[Int((crc ^ UInt32(byte)) & 0xFF)] ^ (crc >> 8) }
        return crc ^ 0xFFFF_FFFF
    }

    /// URL-safe base64 without padding.
    static func base64URL(_ data: Data) -> String {
        data.base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }
}

// MARK: - visualizer.coffee

/// One coffee of visualizer.coffee's canonical bags.
struct CoffeeVisualizerBag: Decodable, Sendable, Equatable, Identifiable {
    var name = ""
    var roaster = ""
    var country = ""
    var region = ""
    var farmer = ""
    var variety = ""
    var processing = ""
    var tastingNotes = ""
    var elevation = ""
    var roastLevel = ""

    var id: String { [name, roaster, country, processing].joined(separator: "|") }

    private enum CodingKeys: String, CodingKey {
        case name, country, region, farmer, variety, processing, elevation
        case roaster = "canonical_roaster_name", tastingNotes = "tasting_notes", roastLevel = "roast_level"
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        func text(_ key: CodingKeys) -> String { (try? c.decodeIfPresent(CoffeeLooseText.self, forKey: key))?.value ?? "" }
        name = text(.name)
        roaster = text(.roaster)
        country = text(.country)
        region = text(.region)
        farmer = text(.farmer)
        variety = text(.variety)
        processing = text(.processing)
        tastingNotes = text(.tastingNotes)
        elevation = text(.elevation)
        roastLevel = text(.roastLevel)
    }

    init() {}
}

/// A JSON string or number as text.
private struct CoffeeLooseText: Decodable {
    let value: String

    init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if let string = try? c.decode(String.self) {
            value = string
        } else if let int = try? c.decode(Int.self) {
            value = String(int)
        } else if let double = try? c.decode(Double.self) {
            value = String(double)
        } else {
            value = ""
        }
    }
}

private struct CoffeeVisualizerReply: Decodable {
    var data: [CoffeeVisualizerBag]

    private enum CodingKeys: String, CodingKey { case data }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        data = (try? c.decodeIfPresent([CoffeeVisualizerBag].self, forKey: .data)) ?? []
    }
}

enum CoffeeVisualizer {
    struct Failure: Error { let message: String }

    /// The first eight canonical coffee bags that match. Needs the phone to be online.
    static func search(_ query: String) async throws -> [CoffeeVisualizerBag] {
        var parts = URLComponents(string: "https://visualizer.coffee/api/canonical_coffee_bags")
        parts?.queryItems = [URLQueryItem(name: "items", value: "8"), URLQueryItem(name: "q", value: query)]
        guard let url = parts?.url else { throw Failure(message: "Bad address") }
        var request = URLRequest(url: url, timeoutInterval: 15)
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        let (data, response) = try await URLSession.shared.data(for: request)
        if let http = response as? HTTPURLResponse, http.statusCode != 200 {
            throw Failure(message: "Visualizer answered \(http.statusCode)")
        }
        return try JSONDecoder().decode(CoffeeVisualizerReply.self, from: data).data
    }

    /// The form's fields from a bag; the roast date stays empty (it is on the owner's bag).
    static func draft(_ bag: CoffeeVisualizerBag) -> CoffeeDraft {
        var coffee = CoffeeDraft()
        coffee.name = CoffeeDraft.clip(bag.name, 23)
        coffee.roaster = CoffeeDraft.clip(bag.roaster, 23)
        coffee.origin = CoffeeDraft.clip(bag.country, 23)
        coffee.region = CoffeeDraft.clip(bag.region, 31)
        coffee.producer = CoffeeDraft.clip(bag.farmer, 31)
        coffee.varietal = CoffeeDraft.clip(bag.variety, 31)
        coffee.process = CoffeeDraft.clip(bag.processing, 15)
        coffee.notes = CoffeeDraft.clip(bag.tastingNotes, 47)
        coffee.altitudeM = CoffeeBeanconqueror.altitude(bag.elevation)
        coffee.roastLevel = roastLevel(bag.roastLevel)
        return coffee
    }

    /// Words to the five levels (WebPage.h VZROAST).
    static func roastLevel(_ text: String) -> Int {
        let s = text.lowercased()
        func has(_ pattern: String) -> Bool { s.range(of: pattern, options: .regularExpression) != nil }
        if has("medium.?dark|full city|vienna|dark|french|italian") {
            return has("medium.?dark|full city") ? 4 : 5
        }
        if has("medium.?light|light.?medium") { return 2 }
        if has("light|cinnamon|nordic") { return 1 }
        if has("medium|city") { return 3 }
        return 0
    }
}
