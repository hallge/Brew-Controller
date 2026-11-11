// Models for the Grinder screen that Core/Models does not have. Lenient like Core/Models/LenientDecoding.swift.
import Foundation

/// The "how advice is worked out" part of GET /api/settings, with `grindRoom` as the number the board uses
/// (0 off, 1 strict, 2 legacy): Core's SettingsModel reads it as a Bool.
struct GrinderAdviceSettings: Codable, Sendable, Equatable {
    var grindToleranceS: Double = 0
    var grindWindow: Int = 0
    var grindRatedTarget = false
    var grindConfidence = false
    var grindRoom: Int = 0
    var grindUmPerS: Double = 0
    var grindMaxStepUm: Int = 0
    /// 0 total time, 1 pour time.
    var grindTimeBasis: Int = 0
    var shareEnabled = false
}

extension GrinderAdviceSettings {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        grindToleranceS = c.lenient(.grindToleranceS, 0)
        grindWindow = c.lenient(.grindWindow, 0)
        grindRatedTarget = c.lenient(.grindRatedTarget, false)
        grindConfidence = c.lenient(.grindConfidence, false)
        if let number: Int = c.lenient(.grindRoom) {
            grindRoom = number
        } else if let flag: Bool = c.lenient(.grindRoom) {
            grindRoom = flag ? 1 : 0
        }
        grindUmPerS = c.lenient(.grindUmPerS, 0)
        grindMaxStepUm = c.lenient(.grindMaxStepUm, 0)
        grindTimeBasis = c.lenient(.grindTimeBasis, 0)
        shareEnabled = c.lenient(.shareEnabled, false)
    }
}

/// GET /api/community/grinders: `{"v":1,"grinders":[...]}`.
struct GrinderSharedList: Codable, Sendable, Equatable {
    var grinders: [GrinderSharedEntry] = []
}

extension GrinderSharedList {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        grinders = c.lenient(.grinders, [])
    }
}

/// A grinder in the shared library.
struct GrinderSharedEntry: Codable, Sendable, Equatable, Identifiable {
    var id: String = ""
    var model: String = ""
    var burrs: String = ""
    var unit: String = ""
    var umPerUnit: Double = 0
    var divisor: Int = 1
    var subSteps: Int = 0
    var min: Int = 0
    var max: Int = 0

    enum CodingKeys: String, CodingKey {
        case id, model, burrs, unit, divisor, min, max
        case umPerUnit = "um_per_unit", subSteps = "sub_steps"
    }
}

extension GrinderSharedEntry {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, "")
        model = c.lenient(.model, "")
        burrs = c.lenient(.burrs, "")
        unit = c.lenient(.unit, "")
        umPerUnit = c.lenient(.umPerUnit, 0)
        divisor = c.lenient(.divisor, 1)
        subSteps = c.lenient(.subSteps, 0)
        min = c.lenient(.min, 0)
        max = c.lenient(.max, 0)
    }
}
