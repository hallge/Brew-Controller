// GET /api/shots (an array of ShotModel, newest first; 409 while a shot is running) and
// GET /api/shot/data?id= (ShotDataModel). Field names from src/net/ShotFormat.h shotToJson and Web.cpp.
import Foundation

struct ShotModel: Codable, Sendable, Equatable, Identifiable {
    var id: Int = 0
    /// ISO 8601 UTC, "2026-11-11T16:38:12Z".
    var timestamp: String = ""
    var recipe: String = ""
    var targetWeightG: Double = 0
    var stopTargetG: Double = 0
    var stopWeightG: Double = 0
    var finalWeightG: Double = 0
    var shotTimeS: Double = 0
    var averageFlowGS: Double = 0
    var peakFlowGS: Double = 0
    var dripWeightG: Double = 0
    var stopOffsetG: Double = 0
    var nextStopOffsetG: Double = 0
    var firstDropS: Double = 0
    var stopLatencyMs: Int = 0
    /// "weight", "weight_predicted", "user", ... (the controller's EndReason names).
    var endReason: String = ""
    var finalValid = false
    var learned = false
    var stopConfirmed = false
    var stopRetried = false
    var learningFactor: Double = 0
    var nextLearningFactor: Double = 0
    /// The stored grind setting (whole steps, 0: none).
    var grindUm: Int = 0
    var doseG: Double = 0
    var targetTimeS: Double = 0
    var pourTimeS: Double = 0
    var pourFlowGS: Double = 0
    /// "total" or "pour".
    var timeBasis: String = "total"
    var coffee: String = ""
    var region: String = ""
    var varietal: String = ""
    var roastLevel: String = ""
    var roastDate: Int = 0
    var preinfusionHoldS: Double = 0
    /// 0: not rated, else 1...5.
    var rating: Int = 0
    /// "", "sour", "balanced" or "bitter".
    var taste: String = ""
    var tdsPct: Double?
    var extractionPct: Double?
    /// "", "ignore" or "count".
    var grindVote: String = ""
    var coffeeId: Int = 0
    var recipeId: Int = 0
    var bloomPauseS: Double = 0
    var grinderCalibration: Int = 0
    var stopStartedPump = false
    var flow25GS: Double = 0
    var flow50GS: Double = 0
    var flow75GS: Double = 0
    var flowAcceleration: Double = 0
    var flowRoughnessGS: Double = 0
    // Only in the list answer:
    var hasData: Bool = false
    /// 0: not weighed.
    var doseWeighedG: Double = 0
    var purged = false
    var uneven = false
    var ambientRhPct: Double?
    var ambientTempC: Double?
    var roomEffC: Double?

    enum CodingKeys: String, CodingKey {
        case id, timestamp, recipe, learned, coffee, region, varietal, rating, taste, purged, uneven
        case targetWeightG = "target_weight_g", stopTargetG = "stop_target_g", stopWeightG = "stop_weight_g"
        case finalWeightG = "final_weight_g", shotTimeS = "shot_time_s", averageFlowGS = "average_flow_g_s"
        case peakFlowGS = "peak_flow_g_s", dripWeightG = "drip_weight_g", stopOffsetG = "stop_offset_g"
        case nextStopOffsetG = "next_stop_offset_g", firstDropS = "first_drop_s", stopLatencyMs = "stop_latency_ms"
        case endReason = "end_reason", finalValid = "final_valid", stopConfirmed = "stop_confirmed"
        case stopRetried = "stop_retried", learningFactor = "learning_factor"
        case nextLearningFactor = "next_learning_factor", grindUm = "grind_um", doseG = "dose_g"
        case targetTimeS = "target_time_s", pourTimeS = "pour_time_s", pourFlowGS = "pour_flow_g_s"
        case timeBasis = "time_basis", roastLevel = "roast_level", roastDate = "roast_date"
        case preinfusionHoldS = "preinfusion_hold_s", tdsPct = "tds_pct", extractionPct = "extraction_pct"
        case grindVote = "grind_vote", coffeeId = "coffee_id", recipeId = "recipe_id"
        case bloomPauseS = "bloom_pause_s", grinderCalibration = "grinder_calibration"
        case stopStartedPump = "stop_started_pump", flow25GS = "flow_25_g_s", flow50GS = "flow_50_g_s"
        case flow75GS = "flow_75_g_s", flowAcceleration = "flow_acceleration", flowRoughnessGS = "flow_roughness_g_s"
        case hasData = "has_data", doseWeighedG = "dose_weighed_g", ambientRhPct = "ambient_rh_pct"
        case ambientTempC = "ambient_temp_c", roomEffC = "room_eff_c"
    }

    /// The shot's time as a date (nil when the clock was not set: the stamp is then 1970).
    var date: Date? {
        let formatter = ISO8601DateFormatter()
        guard let date = formatter.date(from: timestamp), date.timeIntervalSince1970 > 86_400 else { return nil }
        return date
    }
}

extension ShotModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        timestamp = c.lenient(.timestamp, "")
        recipe = c.lenient(.recipe, "")
        targetWeightG = c.lenient(.targetWeightG, 0)
        stopTargetG = c.lenient(.stopTargetG, 0)
        stopWeightG = c.lenient(.stopWeightG, 0)
        finalWeightG = c.lenient(.finalWeightG, 0)
        shotTimeS = c.lenient(.shotTimeS, 0)
        averageFlowGS = c.lenient(.averageFlowGS, 0)
        peakFlowGS = c.lenient(.peakFlowGS, 0)
        dripWeightG = c.lenient(.dripWeightG, 0)
        stopOffsetG = c.lenient(.stopOffsetG, 0)
        nextStopOffsetG = c.lenient(.nextStopOffsetG, 0)
        firstDropS = c.lenient(.firstDropS, 0)
        stopLatencyMs = c.lenient(.stopLatencyMs, 0)
        endReason = c.lenient(.endReason, "")
        finalValid = c.lenient(.finalValid, false)
        learned = c.lenient(.learned, false)
        stopConfirmed = c.lenient(.stopConfirmed, false)
        stopRetried = c.lenient(.stopRetried, false)
        learningFactor = c.lenient(.learningFactor, 0)
        nextLearningFactor = c.lenient(.nextLearningFactor, 0)
        grindUm = c.lenient(.grindUm, 0)
        doseG = c.lenient(.doseG, 0)
        targetTimeS = c.lenient(.targetTimeS, 0)
        pourTimeS = c.lenient(.pourTimeS, 0)
        pourFlowGS = c.lenient(.pourFlowGS, 0)
        timeBasis = c.lenient(.timeBasis, "total")
        coffee = c.lenient(.coffee, "")
        region = c.lenient(.region, "")
        varietal = c.lenient(.varietal, "")
        roastLevel = c.lenient(.roastLevel, "")
        roastDate = c.lenient(.roastDate, 0)
        preinfusionHoldS = c.lenient(.preinfusionHoldS, 0)
        rating = c.lenient(.rating, 0)
        taste = c.lenient(.taste, "")
        tdsPct = c.lenient(.tdsPct)
        extractionPct = c.lenient(.extractionPct)
        grindVote = c.lenient(.grindVote, "")
        coffeeId = c.lenient(.coffeeId, 0)
        recipeId = c.lenient(.recipeId, 0)
        bloomPauseS = c.lenient(.bloomPauseS, 0)
        grinderCalibration = c.lenient(.grinderCalibration, 0)
        stopStartedPump = c.lenient(.stopStartedPump, false)
        flow25GS = c.lenient(.flow25GS, 0)
        flow50GS = c.lenient(.flow50GS, 0)
        flow75GS = c.lenient(.flow75GS, 0)
        flowAcceleration = c.lenient(.flowAcceleration, 0)
        flowRoughnessGS = c.lenient(.flowRoughnessGS, 0)
        hasData = c.lenient(.hasData, false)
        doseWeighedG = c.lenient(.doseWeighedG, 0)
        purged = c.lenient(.purged, false)
        uneven = c.lenient(.uneven, false)
        ambientRhPct = c.lenient(.ambientRhPct)
        ambientTempC = c.lenient(.ambientTempC)
        roomEffC = c.lenient(.roomEffC)
    }
}

/// GET /api/shot/data?id=N: the shot, how well it was recorded, the flow analysis and the curve.
struct ShotDataModel: Codable, Sendable, Equatable {
    var id: Int = 0
    var shot: ShotModel = ShotModel()
    var data: ShotDataStats = ShotDataStats()
    var analysis: ShotAnalysis = ShotAnalysis()
    /// Seconds from the start, one per sample.
    var timeS: [Double] = []
    var weightG: [Double] = []
    var flowGS: [Double] = []
    /// The scale's own timer; null where it had none.
    var scaleTimerS: OptionalSeries = []

    enum CodingKeys: String, CodingKey {
        case id, shot, data, analysis
        case timeS = "time_s", weightG = "weight_g", flowGS = "flow_g_s", scaleTimerS = "scale_timer_s"
    }

    /// The curve as points (time, weight, flow), for a chart.
    var points: [ShotPoint] {
        let count = min(timeS.count, weightG.count)
        return (0..<count).map { index in
            ShotPoint(time: timeS[index], weight: weightG[index],
                      flow: index < flowGS.count ? flowGS[index] : 0)
        }
    }
}

extension ShotDataModel {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, 0)
        shot = c.lenient(.shot, ShotModel())
        data = c.lenient(.data, ShotDataStats())
        analysis = c.lenient(.analysis, ShotAnalysis())
        // A point the controller could not compute is null (the first flow point always is): 0.
        timeS = c.lenient(.timeS, OptionalSeries()).map { $0 ?? 0 }
        weightG = c.lenient(.weightG, OptionalSeries()).map { $0 ?? 0 }
        flowGS = c.lenient(.flowGS, OptionalSeries()).map { $0 ?? 0 }
        scaleTimerS = c.lenient(.scaleTimerS, [])
    }
}

struct ShotPoint: Sendable, Equatable {
    var time: Double
    var weight: Double
    var flow: Double
}

struct ShotDataStats: Codable, Sendable, Equatable {
    var samples: Int = 0
    var durationS: Double = 0
    var meanPeriodMs: Double = 0
    var maxGapMs: Double = 0
    var gapsOver500Ms: Int = 0
    var timerSamples: Int = 0
    var timerSpanS: Double = 0
    var clockSpanS: Double = 0
    var timerLagS: Double = 0

    enum CodingKeys: String, CodingKey {
        case samples
        case durationS = "duration_s", meanPeriodMs = "mean_period_ms", maxGapMs = "max_gap_ms"
        case gapsOver500Ms = "gaps_over_500_ms", timerSamples = "timer_samples", timerSpanS = "timer_span_s"
        case clockSpanS = "clock_span_s", timerLagS = "timer_lag_s"
    }
}

extension ShotDataStats {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        samples = c.lenient(.samples, 0)
        durationS = c.lenient(.durationS, 0)
        meanPeriodMs = c.lenient(.meanPeriodMs, 0)
        maxGapMs = c.lenient(.maxGapMs, 0)
        gapsOver500Ms = c.lenient(.gapsOver500Ms, 0)
        timerSamples = c.lenient(.timerSamples, 0)
        timerSpanS = c.lenient(.timerSpanS, 0)
        clockSpanS = c.lenient(.clockSpanS, 0)
        timerLagS = c.lenient(.timerLagS, 0)
    }
}

/// The flow features worked out again from the stored readings (only when `valid`).
struct ShotAnalysis: Codable, Sendable, Equatable {
    var valid = false
    var flow25GS: Double?
    var flow50GS: Double?
    var flow75GS: Double?
    var flowAcceleration: Double?
    var flowRoughnessGS: Double?

    enum CodingKeys: String, CodingKey {
        case valid
        case flow25GS = "flow_25_g_s", flow50GS = "flow_50_g_s", flow75GS = "flow_75_g_s"
        case flowAcceleration = "flow_acceleration", flowRoughnessGS = "flow_roughness_g_s"
    }
}

extension ShotAnalysis {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        valid = c.lenient(.valid, false)
        flow25GS = c.lenient(.flow25GS)
        flow50GS = c.lenient(.flow50GS)
        flow75GS = c.lenient(.flow75GS)
        flowAcceleration = c.lenient(.flowAcceleration)
        flowRoughnessGS = c.lenient(.flowRoughnessGS)
    }
}

extension BrewAPI {
    /// The newest `limit` shots. Throws a 409 (`isConflict`) while a shot is running.
    func shots(limit: Int = 50) async throws -> [ShotModel] {
        try await get("/api/shots", query: ["limit": String(limit)])
    }

    /// One shot with its curve.
    func shotData(id: Int) async throws -> ShotDataModel {
        try await get("/api/shot/data", query: ["id": String(id)])
    }
}
