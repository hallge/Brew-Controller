// GET /api/grind (Web.cpp handleGrind): the grind advice for the active recipe. snake_case; the
// settings are the grinder's stored whole steps (the "_um" names are historical: format them with the
// grinder's `BrewFormat.Dial`).
import Foundation

struct GrindAdviceModel: Codable, Sendable, Equatable {
    var recipe: String = ""
    /// "no_grind_set", "keep", "finer", "coarser", ... (the advisor's action names).
    var action: String = ""
    var atLimit = false
    var currentUm: Int = 0
    var suggestedUm: Int = 0
    var deltaUm: Int = 0
    var reminderDue = false
    var targetTimeS: Double = 0
    var baseAction: String = ""
    var refined = false
    var ratedState: String = ""
    var ratedShots: Int = 0
    var ratedTimeS: Double = 0
    var ratedBandS: Double = 0
    var aimTimeS: Double = 0
    var ratedTargetOn = false
    var tasteHint: Int = 0
    var tasteShots: Int = 0
    var averageTimeS: Double = 0
    var errorS: Double = 0
    var shotsUsed: Int = 0
    var window: Int = 0
    var toleranceS: Double = 0
    /// "total" or "pour".
    var timeBasis: String = "total"
    var umPerS: Double = 0
    var learned = false
    var learnShots: Int = 0
    var sensitivitySource: String = ""
    var hasNumber = false
    var checkPrep = false
    var checkPrepWhy: String = ""
    var outOfRange = false
    var betweenSteps = false
    var coffee: String = ""
    var beanAgeDays: Int = -1
    var beanFrozen = false
    var confidence: GrindConfidence = GrindConfidence()
    var room: GrindRoom = GrindRoom()
    var lastUm: Int = 0

    enum CodingKeys: String, CodingKey {
        case recipe, action, refined, window, learned, coffee, confidence, room
        case atLimit = "at_limit", currentUm = "current_um", suggestedUm = "suggested_um", deltaUm = "delta_um"
        case reminderDue = "reminder_due", targetTimeS = "target_time_s", baseAction = "base_action"
        case ratedState = "rated_state", ratedShots = "rated_shots", ratedTimeS = "rated_time_s"
        case ratedBandS = "rated_band_s", aimTimeS = "aim_time_s", ratedTargetOn = "rated_target_on"
        case tasteHint = "taste_hint", tasteShots = "taste_shots", averageTimeS = "average_time_s"
        case errorS = "error_s", shotsUsed = "shots_used", toleranceS = "tolerance_s", timeBasis = "time_basis"
        case umPerS = "um_per_s", learnShots = "learn_shots", sensitivitySource = "sensitivity_source"
        case hasNumber = "has_number", checkPrep = "check_prep", checkPrepWhy = "check_prep_why"
        case outOfRange = "out_of_range", betweenSteps = "between_steps", beanAgeDays = "bean_age_days"
        case beanFrozen = "bean_frozen", lastUm = "last_um"
    }
}

extension GrindAdviceModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        recipe = c.lenient(.recipe, "")
        action = c.lenient(.action, "")
        atLimit = c.lenient(.atLimit, false)
        currentUm = c.lenient(.currentUm, 0)
        suggestedUm = c.lenient(.suggestedUm, 0)
        deltaUm = c.lenient(.deltaUm, 0)
        reminderDue = c.lenient(.reminderDue, false)
        targetTimeS = c.lenient(.targetTimeS, 0)
        baseAction = c.lenient(.baseAction, "")
        refined = c.lenient(.refined, false)
        ratedState = c.lenient(.ratedState, "")
        ratedShots = c.lenient(.ratedShots, 0)
        ratedTimeS = c.lenient(.ratedTimeS, 0)
        ratedBandS = c.lenient(.ratedBandS, 0)
        aimTimeS = c.lenient(.aimTimeS, 0)
        ratedTargetOn = c.lenient(.ratedTargetOn, false)
        tasteHint = c.lenient(.tasteHint, 0)
        tasteShots = c.lenient(.tasteShots, 0)
        averageTimeS = c.lenient(.averageTimeS, 0)
        errorS = c.lenient(.errorS, 0)
        shotsUsed = c.lenient(.shotsUsed, 0)
        window = c.lenient(.window, 0)
        toleranceS = c.lenient(.toleranceS, 0)
        timeBasis = c.lenient(.timeBasis, "total")
        umPerS = c.lenient(.umPerS, 0)
        learned = c.lenient(.learned, false)
        learnShots = c.lenient(.learnShots, 0)
        sensitivitySource = c.lenient(.sensitivitySource, "")
        hasNumber = c.lenient(.hasNumber, false)
        checkPrep = c.lenient(.checkPrep, false)
        checkPrepWhy = c.lenient(.checkPrepWhy, "")
        outOfRange = c.lenient(.outOfRange, false)
        betweenSteps = c.lenient(.betweenSteps, false)
        coffee = c.lenient(.coffee, "")
        beanAgeDays = c.lenient(.beanAgeDays, -1)
        beanFrozen = c.lenient(.beanFrozen, false)
        confidence = c.lenient(.confidence, GrindConfidence())
        room = c.lenient(.room, GrindRoom())
        lastUm = c.lenient(.lastUm, 0)
    }
}

struct GrindConfidence: Codable, Sendable, Equatable {
    var score: Double = 0
    var followed: Int = 0
    var rmsS: Double = 0
    var window: Int = 0
    var scaled = false
    var on = false

    enum CodingKeys: String, CodingKey {
        case score, followed, window, scaled, on
        case rmsS = "rms_s"
    }
}

extension GrindConfidence {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        score = c.lenient(.score, 0)
        followed = c.lenient(.followed, 0)
        rmsS = c.lenient(.rmsS, 0)
        window = c.lenient(.window, 0)
        scaled = c.lenient(.scaled, false)
        on = c.lenient(.on, false)
    }
}

/// What the room's temperature does to the brew time, as the advisor has learned it.
struct GrindRoom: Codable, Sendable, Equatable {
    var on = false
    var learned = false
    var sPerC: Double = 0
    var shots: Int = 0
    var applied = false
    var shiftS: Double = 0
    var mode: String = ""

    enum CodingKeys: String, CodingKey {
        case on, learned, shots, applied, mode
        case sPerC = "s_per_c", shiftS = "shift_s"
    }
}

extension GrindRoom {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        on = c.lenient(.on, false)
        learned = c.lenient(.learned, false)
        sPerC = c.lenient(.sPerC, 0)
        shots = c.lenient(.shots, 0)
        applied = c.lenient(.applied, false)
        shiftS = c.lenient(.shiftS, 0)
        mode = c.lenient(.mode, "")
    }
}
