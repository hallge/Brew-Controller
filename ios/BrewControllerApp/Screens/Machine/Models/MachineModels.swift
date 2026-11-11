// Models for the Machine screen that Core/Models does not have. Lenient like Core/Models/LenientDecoding.swift.
import Foundation

/// One line of GET /api/power/events: a press of the power button.
struct MachinePowerEvent: Codable, Sendable, Equatable, Identifiable {
    var t: Int = 0
    var on = false
    /// "schedule", "scale" or "owner".
    var by: String = ""
    var ok = true
    var sure = true
    var outcome: String = ""

    var id: String { "\(t):\(on):\(by)" }
}

extension MachinePowerEvent {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        t = c.lenient(.t, 0)
        on = c.lenient(.on, false)
        by = c.lenient(.by, "")
        ok = c.lenient(.ok, true)
        sure = c.lenient(.sure, true)
        outcome = c.lenient(.outcome, "")
    }
}

/// GET /api/community/machines: `{"v":1,"machines":[...]}`.
struct MachineSharedList: Codable, Sendable, Equatable {
    var machines: [MachineSharedEntry] = []
}

extension MachineSharedList {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        machines = c.lenient(.machines, [])
    }
}

/// A machine profile in the shared library.
struct MachineSharedEntry: Codable, Sendable, Equatable, Identifiable {
    var id: String = ""
    var maker: String = ""
    var model: String = ""
    var control: String = ""
    var style: String = ""
    var manualStart: String = "tap"
    var holdPreinfusion = false
    var tapPreinfusion = false
    var powerToggle = false
    var selfStopS: Int = 0

    enum CodingKeys: String, CodingKey {
        case id, maker, model, control, style
        case manualStart = "manual_start", holdPreinfusion = "hold_preinfusion", tapPreinfusion = "tap_preinfusion"
        case powerToggle = "power_toggle", selfStopS = "self_stop_s"
    }

    /// As an editable profile (not built in, from the library).
    var profile: MachineProfile {
        var p = MachineProfile()
        p.maker = maker
        p.model = model
        p.control = control
        p.style = style
        p.manualStart = manualStart
        p.holdPreinfusion = holdPreinfusion
        p.tapPreinfusion = tapPreinfusion
        p.powerToggle = powerToggle
        p.selfStopS = selfStopS
        p.libraryId = id
        return p
    }
}

extension MachineSharedEntry {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.lenient(.id, "")
        maker = c.lenient(.maker, "")
        model = c.lenient(.model, "")
        control = c.lenient(.control, "")
        style = c.lenient(.style, "toggle_button")
        manualStart = c.lenient(.manualStart, "tap")
        holdPreinfusion = c.lenient(.holdPreinfusion, false)
        tapPreinfusion = c.lenient(.tapPreinfusion, false)
        powerToggle = c.lenient(.powerToggle, false)
        selfStopS = c.lenient(.selfStopS, 0)
    }
}

/// The reviewer's note on the machine in use (`current.review_note` of GET /api/machine; Core's
/// MachineProfile does not carry it).
struct MachineReviewNote: Codable, Sendable, Equatable {
    var note: String = ""

    enum CodingKeys: String, CodingKey { case current }
    enum InnerKeys: String, CodingKey { case reviewNote = "review_note" }
}

extension MachineReviewNote {
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        if let inner = try? c.nestedContainer(keyedBy: InnerKeys.self, forKey: .current) {
            note = inner.lenient(.reviewNote, "")
        }
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        var inner = c.nestedContainer(keyedBy: InnerKeys.self, forKey: .current)
        try inner.encode(note, forKey: .reviewNote)
    }
}

/// How a shot is run, the five styles of the machine profile, in the board's words.
enum MachineStyle: String, CaseIterable, Identifiable, Sendable {
    case toggleButton = "toggle_button"
    case startButton = "start_button"
    case startStopButtons = "start_stop_buttons"
    case holdButton = "hold_button"
    case manual = "manual"

    var id: String { rawValue }

    var label: String {
        switch self {
        case .toggleButton: return "One button: press to start, press to stop"
        case .startButton: return "A button starts; the machine stops itself"
        case .startStopButtons: return "Two buttons: start and stop (adds a stop bot)"
        case .holdButton: return "One button held for the whole shot (SwitchBot)"
        case .manual: return "By hand: lever, knob or paddle"
        }
    }
}
