// GET /api/screen (src/net/ScreenApi.cpp): the controller's screen as data. The words come ready made
// from the firmware; the "tone" fields name a colour role (see BrewPalette.color(forTone:)).
import Foundation

enum ScreenPanel: String, Sendable {
    case home, brew, result, fault
    case grindPrompt = "grind_prompt"
}

struct ScreenSnapshot: Codable, Sendable, Equatable {
    var ok: Bool = true
    /// "home", "brew", "result", "fault" or "grind_prompt".
    var panel: String = "home"
    /// The controller's colour scheme number.
    var theme: Int?
    var live: ScreenLive = ScreenLive()
    var home: ScreenHome = ScreenHome()
    /// Present only while `panel` is "brew".
    var brew: ScreenBrew?
    /// Present only while `panel` is "result".
    var result: ScreenResult?
    /// Present only while `panel` is "fault".
    var fault: ScreenFault?
    /// Present only while `panel` is "grind_prompt".
    var grindPrompt: ScreenGrindPrompt?

    enum CodingKeys: String, CodingKey {
        case ok, panel, theme, live, home, brew, result, fault
        case grindPrompt = "grind_prompt"
    }

    var panelKind: ScreenPanel { ScreenPanel(rawValue: panel) ?? .home }
}

extension ScreenSnapshot {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        ok = c.lenient(.ok, true)
        panel = c.lenient(.panel, "home")
        theme = c.lenient(.theme)
        live = c.lenient(.live, ScreenLive())
        home = c.lenient(.home, ScreenHome())
        brew = c.lenient(.brew)
        result = c.lenient(.result)
        fault = c.lenient(.fault)
        grindPrompt = c.lenient(.grindPrompt)
    }
}

struct ScreenLive: Codable, Sendable, Equatable {
    var state: String = "BOOT"
    var verifyingStop = false
    var holdingButton = false
    var stopStartedPump = false
    var firstDropSeen = false
    var scaleTimerValid = false
    var scaleTimerS: Double = 0
    var bloomLeftS: Double = 0
    var scaleResting = false
    var scaleNear = false
    var targetCue = false
    var manualStopNeeded = false
    var awaitingDrip = false
    var scaleStartPending = false

    enum CodingKeys: String, CodingKey {
        case state
        case verifyingStop = "verifying_stop", holdingButton = "holding_button"
        case stopStartedPump = "stop_started_pump", firstDropSeen = "first_drop_seen"
        case scaleTimerValid = "scale_timer_valid", scaleTimerS = "scale_timer_s"
        case bloomLeftS = "bloom_left_s", scaleResting = "scale_resting", scaleNear = "scale_near"
        case targetCue = "target_cue", manualStopNeeded = "manual_stop_needed"
        case awaitingDrip = "awaiting_drip", scaleStartPending = "scale_start_pending"
    }
}

extension ScreenLive {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        state = c.lenient(.state, "BOOT")
        verifyingStop = c.lenient(.verifyingStop, false)
        holdingButton = c.lenient(.holdingButton, false)
        stopStartedPump = c.lenient(.stopStartedPump, false)
        firstDropSeen = c.lenient(.firstDropSeen, false)
        scaleTimerValid = c.lenient(.scaleTimerValid, false)
        scaleTimerS = c.lenient(.scaleTimerS, 0)
        bloomLeftS = c.lenient(.bloomLeftS, 0)
        scaleResting = c.lenient(.scaleResting, false)
        scaleNear = c.lenient(.scaleNear, false)
        targetCue = c.lenient(.targetCue, false)
        manualStopNeeded = c.lenient(.manualStopNeeded, false)
        awaitingDrip = c.lenient(.awaitingDrip, false)
        scaleStartPending = c.lenient(.scaleStartPending, false)
    }
}

struct ScreenHome: Codable, Sendable, Equatable {
    var status: String = ""
    var hint: String = ""
    var hintIsTouch = false
    var hintTone: String = "text"
    var scaleDot: String = "dim"
    var botDot: String = "dim"
    /// Only when the battery is known.
    var scaleBattery: Int?
    var scaleBatteryTone: String?
    var botBattery: Int?
    var botBatteryTone: String?
    var botWaking = false
    var canStart = false
    var card: ScreenCard = ScreenCard()

    enum CodingKeys: String, CodingKey {
        case status, hint, card
        case hintIsTouch = "hint_is_touch", hintTone = "hint_tone"
        case scaleDot = "scale_dot", botDot = "bot_dot"
        case scaleBattery = "scale_battery", scaleBatteryTone = "scale_battery_tone"
        case botBattery = "bot_battery", botBatteryTone = "bot_battery_tone"
        case botWaking = "bot_waking", canStart = "can_start"
    }
}

extension ScreenHome {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        status = c.lenient(.status, "")
        hint = c.lenient(.hint, "")
        hintIsTouch = c.lenient(.hintIsTouch, false)
        hintTone = c.lenient(.hintTone, "text")
        scaleDot = c.lenient(.scaleDot, "dim")
        botDot = c.lenient(.botDot, "dim")
        scaleBattery = c.lenient(.scaleBattery)
        scaleBatteryTone = c.lenient(.scaleBatteryTone)
        botBattery = c.lenient(.botBattery)
        botBatteryTone = c.lenient(.botBatteryTone)
        botWaking = c.lenient(.botWaking, false)
        canStart = c.lenient(.canStart, false)
        card = c.lenient(.card, ScreenCard())
    }
}

/// The recipe card on the home screen.
struct ScreenCard: Codable, Sendable, Equatable {
    var name: String = ""
    var yieldLine: String = ""
    var yieldRatio: String = ""
    var useRatio = false
    var grind: String = ""
    var advice: String = ""
    var adviceShort: String = ""
    var adviceShortest: String = ""
    var adviceTone: String = "accent"
    var pre: String = ""
    var preShort: String = ""
    var foot: String = ""
    var roastAge: String = ""
    var roastAgeBrief: String = ""

    enum CodingKeys: String, CodingKey {
        case name, grind, advice, pre, foot
        case yieldLine = "yield"
        case yieldRatio = "yield_ratio", useRatio = "use_ratio"
        case adviceShort = "advice_short", adviceShortest = "advice_shortest", adviceTone = "advice_tone"
        case preShort = "pre_short", roastAge = "roast_age", roastAgeBrief = "roast_age_brief"
    }
}

extension ScreenCard {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = c.lenient(.name, "")
        yieldLine = c.lenient(.yieldLine, "")
        yieldRatio = c.lenient(.yieldRatio, "")
        useRatio = c.lenient(.useRatio, false)
        grind = c.lenient(.grind, "")
        advice = c.lenient(.advice, "")
        adviceShort = c.lenient(.adviceShort, "")
        adviceShortest = c.lenient(.adviceShortest, "")
        adviceTone = c.lenient(.adviceTone, "accent")
        pre = c.lenient(.pre, "")
        preShort = c.lenient(.preShort, "")
        foot = c.lenient(.foot, "")
        roastAge = c.lenient(.roastAge, "")
        roastAgeBrief = c.lenient(.roastAgeBrief, "")
    }
}

struct ScreenBrew: Codable, Sendable, Equatable {
    var phase: String = ""
    var stop: String = ""
    var phaseTone: String = "text"
    var running = false
    var clockS: Double = 0
    var showWeight = false
    /// 0...1: how far the weight is to the target.
    var ringProgress: Double = 0

    enum CodingKeys: String, CodingKey {
        case phase, stop, running
        case phaseTone = "phase_tone", clockS = "clock_s", showWeight = "show_weight"
        case ringProgress = "ring_progress"
    }
}

extension ScreenBrew {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        phase = c.lenient(.phase, "")
        stop = c.lenient(.stop, "")
        phaseTone = c.lenient(.phaseTone, "text")
        running = c.lenient(.running, false)
        clockS = c.lenient(.clockS, 0)
        showWeight = c.lenient(.showWeight, false)
        ringProgress = c.lenient(.ringProgress, 0)
    }
}

struct ScreenResult: Codable, Sendable, Equatable {
    var have = false
    var abnormal = false
    var timedOut = false
    var top: String?
    var topTone: String?
    var weight: String?
    var line1: String?
    /// The longest wording first; use the first that fits.
    var line2: [String] = []
    var shotId: Int?
    var review = false
    var grindChange = false
    var counted = false
    var reviewText: String?
    var reviewTextShort: String?
    var grind: String?
    var grindSub: String?
    var grindTone: String?
    var autoAck = false

    enum CodingKeys: String, CodingKey {
        case have, abnormal, top, weight, line1, line2, review, counted, grind
        case timedOut = "timed_out", topTone = "top_tone", shotId = "shot_id"
        case grindChange = "grind_change", reviewText = "review_text", reviewTextShort = "review_text_short"
        case grindSub = "grind_sub", grindTone = "grind_tone", autoAck = "auto_ack"
    }
}

extension ScreenResult {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        have = c.lenient(.have, false)
        abnormal = c.lenient(.abnormal, false)
        timedOut = c.lenient(.timedOut, false)
        top = c.lenient(.top)
        topTone = c.lenient(.topTone)
        weight = c.lenient(.weight)
        line1 = c.lenient(.line1)
        line2 = c.lenient(.line2, [])
        shotId = c.lenient(.shotId)
        review = c.lenient(.review, false)
        grindChange = c.lenient(.grindChange, false)
        counted = c.lenient(.counted, false)
        reviewText = c.lenient(.reviewText)
        reviewTextShort = c.lenient(.reviewTextShort)
        grind = c.lenient(.grind)
        grindSub = c.lenient(.grindSub)
        grindTone = c.lenient(.grindTone)
        autoAck = c.lenient(.autoAck, false)
    }
}

struct ScreenFault: Codable, Sendable, Equatable {
    var title: String = ""
    var message: String = ""
    var offerPress = false

    enum CodingKeys: String, CodingKey {
        case title, message
        case offerPress = "offer_press"
    }
}

extension ScreenFault {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        title = c.lenient(.title, "")
        message = c.lenient(.message, "")
        offerPress = c.lenient(.offerPress, false)
    }
}

/// The grind question (kind: "new_advice", "no_grind" or "reminder").
struct ScreenGrindPrompt: Codable, Sendable, Equatable {
    var kind: String = ""
    var title: String = ""
    var setting: String = ""
    var why: String = ""
    var use: String = ""
    var keep: String = ""
    var useUm: Int = 0

    enum CodingKeys: String, CodingKey {
        case kind, title, setting, why, use, keep
        case useUm = "use_um"
    }
}

extension ScreenGrindPrompt {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        kind = c.lenient(.kind, "")
        title = c.lenient(.title, "")
        setting = c.lenient(.setting, "")
        why = c.lenient(.why, "")
        use = c.lenient(.use, "")
        keep = c.lenient(.keep, "")
        useUm = c.lenient(.useUm, 0)
    }
}
