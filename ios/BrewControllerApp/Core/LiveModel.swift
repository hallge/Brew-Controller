// LiveModel: the 10 Hz live status (bbw_live), the /api/screen refresh on each change, and the
// controller's events, for every screen of the app.
import Foundation
import Observation

// MARK: - State

/// bbw::State, in the controller's order (src/core/Types.h).
enum BrewState: Int, Sendable, CaseIterable {
    case boot = 0, initializing, connectingScale, ready, taring, waitingForCup, readyToBrew
    case startingShot, brewing, blooming, stoppingShot, postShotDrip, shotComplete
    case scaleDisconnected, switchbotDisconnected, scaleDataStale, startCommandFailed, stopCommandFailed
    case tareFailed, shotTimeout, invalidWeight, bleError

    /// A shot is in hand: the machine may be running (the web API's "busy brewing").
    var isShotInHand: Bool {
        switch self {
        case .startingShot, .brewing, .blooming, .stoppingShot, .postShotDrip: return true
        default: return false
        }
    }

    /// One of the fault states.
    var isFault: Bool { rawValue >= BrewState.scaleDisconnected.rawValue }
}

/// bbw_command codes.
enum BrewCommand: Int32, Sendable {
    case start = 1
    case stop = 2
    case tare = 3
    case acknowledge = 4
    case press = 5
    case activity = 6
    case nextRecipe = 7
}

// MARK: - BrewLive

/// A Swift copy of `BbwLive` (ios/Host/BrewHost.h).
struct BrewLive: Sendable, Equatable {
    var change: UInt32 = 0
    var stateNumber: Int = 0
    var stateName: String = "BOOT"
    var scaleConnected = false
    var botConnected = false
    var weightFresh = false
    var weightG: Double = 0
    var flowGS: Double = 0
    var shotTimeS: Double = 0
    var targetWeightG: Double = 0
    var stopOffsetG: Double = 0
    var samplePeriodMs: Int = 0
    var manualStopNeeded = false
    var verifyingStop = false
    var holdingButton = false
    var stopStartedPump = false
    var machineStyle: Int = 0
    var targetCue = false
    var awaitingDrip = false
    var cupPresent = false
    var cupRequired = false
    var activeRecipe: Int = 0
    var recipe: String = ""
    var useRatio = false
    var doseG: Double = 0
    var ratio: Double = 0
    var firstDropSeen = false
    var botPaired = false
    var scaleTimerValid = false
    var scaleTimerS: Double = 0
    /// -1: not reported.
    var scaleBatteryPercent: Int = -1
    /// -1: never read.
    var botBatteryPercent: Int = -1
    var bloomLeftS: Double = 0
    var scaleResting = false
    var scaleStartPending = false
    var scaleNearby = false
    /// 0: none yet.
    var lastShotID: UInt32 = 0
    var theme: Int = 0
    var restartRequested = false
    var demo = false

    /// The state as an enum (boot for a number this app does not know).
    var state: BrewState { BrewState(rawValue: stateNumber) ?? .boot }
    var isShotInHand: Bool { state.isShotInHand }

    init() {}

    init(_ c: BbwLive) {
        change = c.change
        stateNumber = Int(c.state)
        stateName = BrewLive.string(c.state_name)
        scaleConnected = c.scale_connected != 0
        botConnected = c.bot_connected != 0
        weightFresh = c.weight_fresh != 0
        weightG = Double(c.weight_g)
        flowGS = Double(c.flow_gs)
        shotTimeS = Double(c.shot_time_s)
        targetWeightG = Double(c.target_weight_g)
        stopOffsetG = Double(c.stop_offset_g)
        samplePeriodMs = Int(c.sample_period_ms)
        manualStopNeeded = c.manual_stop_needed != 0
        verifyingStop = c.verifying_stop != 0
        holdingButton = c.holding_button != 0
        stopStartedPump = c.stop_started_pump != 0
        machineStyle = Int(c.machine_style)
        targetCue = c.target_cue != 0
        awaitingDrip = c.awaiting_drip != 0
        cupPresent = c.cup_present != 0
        cupRequired = c.cup_required != 0
        activeRecipe = Int(c.active_recipe)
        recipe = BrewLive.string(c.recipe)
        useRatio = c.use_ratio != 0
        doseG = Double(c.dose_g)
        ratio = Double(c.ratio)
        firstDropSeen = c.first_drop_seen != 0
        botPaired = c.bot_paired != 0
        scaleTimerValid = c.scale_timer_valid != 0
        scaleTimerS = Double(c.scale_timer_s)
        scaleBatteryPercent = Int(c.scale_battery_percent)
        botBatteryPercent = Int(c.bot_battery_percent)
        bloomLeftS = Double(c.bloom_left_s)
        scaleResting = c.scale_resting != 0
        scaleStartPending = c.scale_start_pending != 0
        scaleNearby = c.scale_nearby != 0
        lastShotID = c.last_shot_id
        theme = Int(c.theme)
        restartRequested = c.restart_requested != 0
        demo = c.demo != 0
    }

    /// Reads the controller now. Safe from any thread.
    static func read() -> BrewLive {
        var raw = BbwLive()
        bbw_live(&raw)
        return BrewLive(raw)
    }

    /// A NUL-terminated C char array (imported as a tuple) as a String.
    private static func string<T>(_ tuple: T) -> String {
        withUnsafeBytes(of: tuple) { raw in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
    }
}

// MARK: - Events

/// The controller's events, as Swift notifications (posted on the main thread).
extension Notification.Name {
    /// userInfo: `BrewEventKey.state` (Int, the new bbw::State).
    static let brewStateChanged = Notification.Name("bbw.event.state")
    /// The controller wants to restart (a restore, a switch of demo/real): show "tap to restart".
    static let brewRestartRequested = Notification.Name("bbw.event.restart")
    /// The screen would wake (a button, a state change worth waking for).
    static let brewWake = Notification.Name("bbw.event.wake")
}

enum BrewEventKey {
    static let state = "state"
}

/// The event as a value.
enum BrewEvent: Sendable, Equatable {
    case state(BrewState)
    case restart
    case wake
}

/// Carries the C callback's calls to Swift. The callback runs on a host thread.
private final class BrewEventBridge: Sendable {
    let handler: @Sendable (BrewEvent) -> Void
    init(handler: @escaping @Sendable (BrewEvent) -> Void) { self.handler = handler }
}

/// The C callback: no captures, so it is a plain function pointer.
private let brewEventTrampoline: @convention(c) (Int32, Int32, UnsafeMutableRawPointer?) -> Void = { event, arg, context in
    guard let context else { return }
    let bridge = Unmanaged<BrewEventBridge>.fromOpaque(context).takeUnretainedValue()
    // BBW_EVENT_STATE = 1, BBW_EVENT_RESTART = 2, BBW_EVENT_WAKE = 3 (BrewHost.h)
    switch Int(event) {
    case 1: bridge.handler(.state(BrewState(rawValue: Int(arg)) ?? .boot))
    case 2: bridge.handler(.restart)
    case 3: bridge.handler(.wake)
    default: break
    }
}

// MARK: - LiveModel

@MainActor
@Observable
final class LiveModel {
    static let shared = LiveModel()

    /// The latest live status.
    private(set) var live = BrewLive()
    /// The latest /api/screen answer (nil until the first one arrives).
    private(set) var screen: ScreenSnapshot?
    /// The last event, for a screen that wants to react to one.
    private(set) var lastEvent: BrewEvent?
    /// The controller asked to restart (restore, switching demo/real).
    private(set) var restartRequested = false
    /// The app is in the foreground (set by `AppLifecycle`).
    private(set) var isActive = true
    /// The last error of a screen refresh, for a quiet "not connected" line.
    private(set) var screenError: BrewAPIError?

    /// Called on the main actor after every reading that changed (`AppLifecycle` hooks the idle-timer rules in here).
    @ObservationIgnored var onLiveChanged: (@MainActor (BrewLive) -> Void)?

    @ObservationIgnored private let api: BrewAPI
    @ObservationIgnored private var pollTask: Task<Void, Never>?
    @ObservationIgnored private var screenTask: Task<Void, Never>?
    @ObservationIgnored private var lastChange: UInt32?
    @ObservationIgnored private var screenDirty = true
    @ObservationIgnored private var lastScreenRequest: ContinuousClock.Instant?
    @ObservationIgnored private var bridge: BrewEventBridge?
    @ObservationIgnored private var bridgePointer: UnsafeMutableRawPointer?

    /// Foreground rate and the background rate while a shot is in hand.
    static let foregroundInterval: Duration = .milliseconds(100)
    static let backgroundInterval: Duration = .milliseconds(500)
    /// At most 5 refreshes of /api/screen a second.
    static let minScreenInterval: Duration = .milliseconds(200)

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    // MARK: Start and stop

    /// Registers the event callback and starts polling. Call once the host is started.
    func start() {
        registerEvents()
        guard pollTask == nil else { return }
        pollTask = Task { [weak self] in
            await self?.pollLoop()
        }
    }

    /// Stops polling and the event callback.
    func stop() {
        pollTask?.cancel()
        pollTask = nil
        screenTask?.cancel()
        screenTask = nil
        unregisterEvents()
    }

    /// Foreground (true) or background (false). Background polls at 2 Hz only while a shot is in hand.
    func setActive(_ active: Bool) {
        guard active != isActive else { return }
        isActive = active
        if active { screenDirty = true }
    }

    // MARK: Commands

    /// Queues a command (bbw_command); returns false for a command the controller does not know.
    @discardableResult
    func send(_ command: BrewCommand) -> Bool {
        bbw_command(command.rawValue) == 0
    }

    /// Asks for a fresh /api/screen at the next opportunity (after an action that changed it).
    func requestScreenRefresh() {
        screenDirty = true
    }

    // MARK: Polling

    private func pollLoop() async {
        while !Task.isCancelled {
            if isActive || live.isShotInHand {
                await pollOnce()
                try? await Task.sleep(for: isActive ? Self.foregroundInterval : Self.backgroundInterval)
            } else {
                // Background, nothing in hand: nothing to show, so no polling. Look again in a second.
                try? await Task.sleep(for: .seconds(1))
            }
        }
    }

    private func pollOnce() async {
        let next = await Task.detached(priority: .userInitiated) { BrewLive.read() }.value
        apply(next)
    }

    /// Takes a new reading in (also used by tests).
    func apply(_ next: BrewLive) {
        if lastChange != next.change {
            lastChange = next.change
            screenDirty = true
        }
        if next != live {
            live = next
            onLiveChanged?(next)
        }
        if next.restartRequested && !restartRequested { restartRequested = true }
        BrewTheme.shared.controllerThemeChanged(next.theme)
        if isActive { refreshScreenIfDue() }
    }

    private func refreshScreenIfDue() {
        guard screenDirty, screenTask == nil else { return }
        let now = ContinuousClock.now
        if let last = lastScreenRequest, now - last < Self.minScreenInterval { return }
        screenDirty = false
        lastScreenRequest = now
        screenTask = Task { [weak self] in
            guard let self else { return }
            await self.fetchScreen()
        }
    }

    private func fetchScreen() async {
        defer { screenTask = nil }
        do {
            let snapshot: ScreenSnapshot = try await api.get("/api/screen")
            screen = snapshot
            screenError = nil
            if let theme = snapshot.theme { BrewTheme.shared.controllerThemeChanged(theme) }
        } catch let error as BrewAPIError {
            screenError = error
            screenDirty = true  // try again at the next poll
        } catch {
            screenError = .decoding(String(describing: error))
        }
    }

    // MARK: Events

    private func registerEvents() {
        guard bridge == nil else { return }
        let bridge = BrewEventBridge { event in
            Task { @MainActor in
                LiveModel.shared.handle(event)
            }
        }
        let pointer = Unmanaged.passRetained(bridge).toOpaque()
        self.bridge = bridge
        bridgePointer = pointer
        bbw_set_event_callback(brewEventTrampoline, pointer)
    }

    private func unregisterEvents() {
        bbw_set_event_callback(nil, nil)
        if let pointer = bridgePointer {
            Unmanaged<BrewEventBridge>.fromOpaque(pointer).release()
        }
        bridgePointer = nil
        bridge = nil
    }

    /// An event arrived (main actor): remember it and post the notification.
    func handle(_ event: BrewEvent) {
        lastEvent = event
        screenDirty = true
        let center = NotificationCenter.default
        switch event {
        case .state(let state):
            center.post(name: .brewStateChanged, object: nil, userInfo: [BrewEventKey.state: state.rawValue])
        case .restart:
            restartRequested = true
            center.post(name: .brewRestartRequested, object: nil)
        case .wake:
            center.post(name: .brewWake, object: nil)
        }
    }
}
