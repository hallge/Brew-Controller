// GET /api/addons (src/addons/AddonHost.cpp listJson): the add-ons compiled in or loaded as modules.
// Lenient like Core/Models/LenientDecoding.swift. The steering and comparison "card" an add-on may declare
// and the module delivery (repository, upload) are not modelled: the screen does not draw them.
import Foundation

struct SetupAddonField: Codable, Sendable, Equatable, Identifiable {
    var key = ""
    var label = ""
    /// "bool", "secret", or text.
    var type = ""
    var help = ""
    var value = ""
    var set = false
    var id: String { key }

    enum CodingKeys: String, CodingKey { case key, label, type, help, value, set }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        key = c.lenient(.key, "")
        label = c.lenient(.label, "")
        type = c.lenient(.type, "")
        help = c.lenient(.help, "")
        set = c.lenient(.set, false)
        // The value is text, but tolerate a number or a bool.
        if let text: String = c.lenient(.value) {
            value = text
        } else if let number: Int = c.lenient(.value) {
            value = String(number)
        } else if let flag: Bool = c.lenient(.value) {
            value = flag ? "1" : "0"
        }
    }
}

struct SetupAddonMapEntry: Codable, Sendable, Equatable {
    var grinderRecipe = 0
    var recipeId = 0
    var source = ""

    enum CodingKeys: String, CodingKey {
        case grinderRecipe = "grinder_recipe", recipeId = "recipe_id", source
    }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        grinderRecipe = c.lenient(.grinderRecipe, 0)
        recipeId = c.lenient(.recipeId, 0)
        source = c.lenient(.source, "")
    }
}

struct SetupAddonOptions: Codable, Sendable, Equatable {
    var doseSetsTarget = false
    var selectRecipe = false
    var shotFeedback = false

    enum CodingKeys: String, CodingKey {
        case doseSetsTarget = "dose_sets_target", selectRecipe = "select_recipe", shotFeedback = "shot_feedback"
    }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        doseSetsTarget = c.lenient(.doseSetsTarget, false)
        selectRecipe = c.lenient(.selectRecipe, false)
        shotFeedback = c.lenient(.shotFeedback, false)
    }
}

struct SetupAddonLastGrind: Codable, Sendable, Equatable {
    var ageS = 0
    var grinderRecipe = 0
    var doseG: Double?
    var gapUm: Double?
    var mapped = false
    var recipeId = 0
    var setting = ""
    var shotTargetG: Double = 0
    var note = ""

    enum CodingKeys: String, CodingKey {
        case ageS = "age_s", grinderRecipe = "grinder_recipe", doseG = "dose_g", gapUm = "gap_um", mapped
        case recipeId = "recipe_id", setting, shotTargetG = "shot_target_g", note
    }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        ageS = c.lenient(.ageS, 0)
        grinderRecipe = c.lenient(.grinderRecipe, 0)
        doseG = c.lenient(.doseG)
        gapUm = c.lenient(.gapUm)
        mapped = c.lenient(.mapped, false)
        recipeId = c.lenient(.recipeId, 0)
        // "setting" is the steps adopted: a number on the controller, shown as given.
        if let text: String = c.lenient(.setting) {
            setting = text
        } else if let number: Int = c.lenient(.setting) {
            setting = number > 0 ? String(number) : ""
        }
        shotTargetG = c.lenient(.shotTargetG, 0)
        note = c.lenient(.note, "")
    }
}

struct SetupAddon: Codable, Sendable, Equatable, Identifiable {
    var id = ""
    var name = ""
    var version = ""
    var kind = ""
    var apiOk = true
    var enabled = false
    var link = ""
    var status = ""
    var shotFeedbackPossible = false
    var fields: [SetupAddonField] = []
    var options = SetupAddonOptions()
    var map: [SetupAddonMapEntry] = []
    var lastGrind: SetupAddonLastGrind?

    enum CodingKeys: String, CodingKey {
        case id, name, version, kind, enabled, link, status, fields, options, map
        case apiOk = "api_ok", shotFeedbackPossible = "shot_feedback_possible", lastGrind = "last_grind"
    }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, "")
        name = c.lenient(.name, "")
        version = c.lenient(.version, "")
        kind = c.lenient(.kind, "")
        apiOk = c.lenient(.apiOk, true)
        enabled = c.lenient(.enabled, false)
        link = c.lenient(.link, "")
        status = c.lenient(.status, "")
        shotFeedbackPossible = c.lenient(.shotFeedbackPossible, false)
        fields = c.lenient(.fields, [])
        options = c.lenient(.options, SetupAddonOptions())
        map = c.lenient(.map, [])
        lastGrind = c.lenient(.lastGrind)
    }
}

struct SetupAddonList: Codable, Sendable, Equatable {
    var compiled = 0
    var addons: [SetupAddon] = []

    enum CodingKeys: String, CodingKey { case compiled, addons }

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        compiled = c.lenient(.compiled, 0)
        addons = c.lenient(.addons, [])
    }
}
