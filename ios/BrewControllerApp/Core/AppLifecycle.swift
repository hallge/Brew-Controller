// AppLifecycle: scene phase to the controller, the idle-timer and dim rules that replace the board's
// own screen dimming and standby, background time mid-shot, and the restart flow.
import Observation
import SwiftUI
import UIKit
import UserNotifications

@MainActor
@Observable
final class AppLifecycle {
    static let shared = AppLifecycle()

    /// Seconds after the scale goes before the phone may auto-lock again.
    static let autoLockDelay: Duration = .seconds(30)
    /// Idle time before the dim overlay (the board's dimming).
    static let dimAfter: Duration = .seconds(180)

    /// The scene is active.
    private(set) var isForeground = true
    /// The overlay that dims the screen after 3 minutes without a touch. Any touch clears it.
    private(set) var isDimmed = false
    /// The restart flow is showing: "Restored, tap to restart".
    private(set) var restartPending = false
    /// The words for the restart banner.
    let restartMessage = "Restored, tap to restart"

    @ObservationIgnored private let live: LiveModel
    @ObservationIgnored private var autoLockTask: Task<Void, Never>?
    @ObservationIgnored private var dimTask: Task<Void, Never>?
    @ObservationIgnored private var observers: [NSObjectProtocol] = []
    @ObservationIgnored private var backgroundTaskID: UIBackgroundTaskIdentifier = .invalid
    @ObservationIgnored private var keepAwake = false
    @ObservationIgnored private var started = false
    @ObservationIgnored private var lastActivityCommand: ContinuousClock.Instant?

    init(live: LiveModel = .shared) {
        self.live = live
    }

    // MARK: Start

    /// Wires the notifications and applies the current state. Call once from the app's root.
    func start() {
        guard !started else { return }
        started = true
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: .brewRestartRequested, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.restartRequested() }
        })
        observers.append(center.addObserver(forName: .brewWake, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.userActivity() }
        })
        observers.append(center.addObserver(forName: .brewStateChanged, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in self?.liveChanged() }
        })
        live.onLiveChanged = { [weak self] _ in self?.liveChanged() }
        scheduleDim()
        liveChanged()
    }

    // MARK: Scene phase

    /// The SwiftUI scene phase changed.
    func scenePhaseChanged(_ phase: ScenePhase) {
        switch phase {
        case .active:
            isForeground = true
            bbw_set_foreground(1)
            live.setActive(true)
            endBackgroundTask()
            userActivity()
        case .inactive:
            // Inactive is transient (a call, the app switcher): the controller keeps running at full speed.
            break
        case .background:
            isForeground = false
            beginBackgroundTaskIfShotInHand()
            bbw_set_foreground(0)
            live.setActive(false)
            dimTask?.cancel()
            isDimmed = false
        @unknown default:
            break
        }
        liveChanged()
    }

    // MARK: Idle timer and dim

    /// Call from the root view on every touch: clears the dim overlay and restarts the 3-minute count,
    /// and tells the controller at most every 5 s (BBW_CMD_ACTIVITY keeps an idle scale awake).
    func userActivity() {
        let now = ContinuousClock.now
        if lastActivityCommand.map({ now - $0 >= .seconds(5) }) ?? true {
            lastActivityCommand = now
            live.send(.activity)
        }
        if isDimmed { isDimmed = false }
        scheduleDim()
    }

    /// The live status changed (call on each change, or leave it to the notification): applies the
    /// idle-timer rules. The idle timer is off while the scale is connected or a shot is in hand; the
    /// phone may auto-lock 30 s after the scale goes.
    func liveChanged() {
        let needed = live.live.scaleConnected || live.live.isShotInHand
        if needed {
            autoLockTask?.cancel()
            autoLockTask = nil
            setKeepAwake(true)
        } else if keepAwake && autoLockTask == nil {
            autoLockTask = Task { [weak self] in
                try? await Task.sleep(for: AppLifecycle.autoLockDelay)
                guard !Task.isCancelled, let self else { return }
                self.autoLockTask = nil
                if !self.live.live.scaleConnected && !self.live.live.isShotInHand { self.setKeepAwake(false) }
            }
        }
        if live.live.restartRequested { restartRequested() }
    }

    private func setKeepAwake(_ on: Bool) {
        keepAwake = on
        UIApplication.shared.isIdleTimerDisabled = on
    }

    private func scheduleDim() {
        dimTask?.cancel()
        guard isForeground else { return }
        dimTask = Task { [weak self] in
            try? await Task.sleep(for: AppLifecycle.dimAfter)
            guard !Task.isCancelled, let self else { return }
            // Dimming only matters while the screen is being kept awake: otherwise the phone locks itself.
            if self.keepAwake { self.isDimmed = true }
        }
    }

    // MARK: Background time

    private func beginBackgroundTaskIfShotInHand() {
        guard live.live.isShotInHand, backgroundTaskID == .invalid else { return }
        backgroundTaskID = UIApplication.shared.beginBackgroundTask(withName: "bbw.shot") { [weak self] in
            // Time is up: end the task (the scale's notifications keep the app running after this).
            Task { @MainActor in self?.endBackgroundTask() }
        }
    }

    private func endBackgroundTask() {
        guard backgroundTaskID != .invalid else { return }
        UIApplication.shared.endBackgroundTask(backgroundTaskID)
        backgroundTaskID = .invalid
    }

    // MARK: Restart flow

    private func restartRequested() {
        restartPending = true
        isDimmed = false
    }

    /// The owner tapped "Restored, tap to restart": schedules the local notification "Tap to reopen
    /// Brew Controller" and ends the process, since the firmware cannot run setup() twice. The
    /// notification is shown a second or two later, so the owner can tap it to come back.
    func confirmRestart() async {
        let center = UNUserNotificationCenter.current()
        _ = try? await center.requestAuthorization(options: [.alert, .sound])
        let content = UNMutableNotificationContent()
        content.title = "Tap to reopen Brew Controller"
        content.sound = .default
        let request = UNNotificationRequest(identifier: "bbw.restart", content: content,
                                            trigger: UNTimeIntervalNotificationTrigger(timeInterval: 1, repeats: false))
        try? await center.add(request)
        exit(0)
    }
}

/// The root view uses this to feed touches into `AppLifecycle.userActivity()`.
///
/// Not a SwiftUI gesture: a `DragGesture(minimumDistance: 0)` on the root, even as a simultaneous
/// gesture, swallows taps in Lists and Forms on iOS 18 (More's rows, pickers, add buttons). Instead a
/// UIKit recognizer on the window sees every touch begin and then fails at once, so it never delays,
/// cancels or competes with any control.
extension View {
    func brewTracksActivity(_ lifecycle: AppLifecycle) -> some View {
        background(BrewTouchObserver { lifecycle.userActivity() }.frame(width: 0, height: 0))
    }
}

/// Puts a `BrewTouchRecognizer` on the window the view is in.
private struct BrewTouchObserver: UIViewRepresentable {
    let onTouch: @MainActor () -> Void

    func makeUIView(context: Context) -> BrewTouchHostView {
        let view = BrewTouchHostView()
        view.onTouch = onTouch
        view.isUserInteractionEnabled = false
        return view
    }

    func updateUIView(_ uiView: BrewTouchHostView, context: Context) {
        uiView.onTouch = onTouch
    }
}

final class BrewTouchHostView: UIView {
    var onTouch: (@MainActor () -> Void)?
    private var recognizer: BrewTouchRecognizer?

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if let recognizer { recognizer.view?.removeGestureRecognizer(recognizer) }
        recognizer = nil
        guard let window else { return }
        let added = BrewTouchRecognizer(target: nil, action: nil)
        added.onTouch = { [weak self] in self?.onTouch?() }
        window.addGestureRecognizer(added)
        recognizer = added
    }
}

/// Reports a touch and fails straight away: it never recognises, so every other gesture and control
/// gets the touch exactly as without it.
final class BrewTouchRecognizer: UIGestureRecognizer, UIGestureRecognizerDelegate {
    var onTouch: (@MainActor () -> Void)?

    override init(target: Any?, action: Selector?) {
        super.init(target: target, action: action)
        cancelsTouchesInView = false
        delaysTouchesBegan = false
        delaysTouchesEnded = false
        delegate = self
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent) {
        onTouch?()
        state = .failed
    }

    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool { true }
}
