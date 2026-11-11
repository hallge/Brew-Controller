// Onboarding state, logic only (the views come later): the safety notice, demo or real devices,
// Bluetooth permission, and diagnostics consent. Stored in UserDefaults.
import Foundation
import Observation

/// Bluetooth state as the app needs it (BbwBluetoothState, ios/Host/ble_corebluetooth.h).
enum BrewBluetoothState: Int, Sendable {
    case unknown = 0, poweredOn, poweredOff, unauthorized, unsupported

    /// Real devices can be used.
    var isUsable: Bool { self == .poweredOn }
    /// The owner has to go to Settings (denied or restricted).
    var needsSettings: Bool { self == .unauthorized }

    static func current() -> BrewBluetoothState {
        BrewBluetoothState(rawValue: Int(bbw_corebluetooth_state())) ?? .unknown
    }
}

/// The steps of the first run, in order.
enum OnboardingStep: Sendable, Equatable {
    case safetyNotice
    case chooseMode
    case bluetooth
    case diagnostics
    case done
}

enum DeviceMode: String, Sendable {
    case demo
    case real
}

@MainActor
@Observable
final class Onboarding {
    static let shared = Onboarding()

    private enum Key {
        static let safetyAccepted = "bbw.onboarding.safetyAccepted"
        static let mode = "bbw.onboarding.mode"
        static let diagnosticsAnswered = "bbw.onboarding.diagnosticsAnswered"
        static let bluetoothShown = "bbw.onboarding.bluetoothShown"
    }

    /// The safety notice was accepted.
    private(set) var safetyAccepted: Bool
    /// Demo or real devices; nil until chosen.
    private(set) var mode: DeviceMode?
    /// The diagnostics question was answered (either way).
    private(set) var diagnosticsAnswered: Bool
    /// The Bluetooth step was shown (the system prompt appears when the central is created).
    private(set) var bluetoothShown: Bool
    /// The Bluetooth permission state, refreshed by `refreshBluetooth()`.
    private(set) var bluetooth: BrewBluetoothState = .unknown
    /// The last diagnostics consent error, for the view.
    private(set) var consentError: BrewAPIError?

    @ObservationIgnored private let defaults: UserDefaults
    @ObservationIgnored private let api: BrewAPI
    /// Reads the Bluetooth state; replaced in tests.
    @ObservationIgnored var bluetoothReader: @MainActor () -> BrewBluetoothState = { BrewBluetoothState.current() }

    init(defaults: UserDefaults = .standard, api: BrewAPI = .shared) {
        self.defaults = defaults
        self.api = api
        safetyAccepted = defaults.bool(forKey: Key.safetyAccepted)
        mode = defaults.string(forKey: Key.mode).flatMap(DeviceMode.init(rawValue:))
        diagnosticsAnswered = defaults.bool(forKey: Key.diagnosticsAnswered)
        bluetoothShown = defaults.bool(forKey: Key.bluetoothShown)
    }

    // MARK: Where the first run stands

    /// The next step to show, in order: notice, mode, Bluetooth (real mode only), diagnostics.
    var step: OnboardingStep {
        if !safetyAccepted { return .safetyNotice }
        guard let mode else { return .chooseMode }
        if mode == .real && !bluetoothShown && !bluetooth.isUsable { return .bluetooth }
        if !diagnosticsAnswered { return .diagnostics }
        return .done
    }

    var isComplete: Bool { step == .done }

    /// The data folder name for the mode: demo and real keep separate flash (a restart switches them).
    var dataFolderName: String { mode == .demo ? "Controller-demo" : "Controller" }

    // MARK: Actions

    func acceptSafetyNotice() {
        safetyAccepted = true
        defaults.set(true, forKey: Key.safetyAccepted)
    }

    /// The first run's choice. Before the host starts nothing else is needed; after it, use `switchMode`.
    func chooseMode(_ newMode: DeviceMode) {
        mode = newMode
        defaults.set(newMode.rawValue, forKey: Key.mode)
    }

    /// Switching demo/real later: stores the choice and returns true when the controller has to restart
    /// (the data folder differs). The caller then runs the restart flow (`AppLifecycle.confirmRestart`).
    @discardableResult
    func switchMode(_ newMode: DeviceMode) -> Bool {
        guard newMode != mode else { return false }
        chooseMode(newMode)
        return true
    }

    /// Reads the Bluetooth state now.
    @discardableResult
    func refreshBluetooth() -> BrewBluetoothState {
        bluetooth = bluetoothReader()
        return bluetooth
    }

    /// The Bluetooth step has been shown to the owner (they pressed on).
    func bluetoothStepFinished() {
        bluetoothShown = true
        defaults.set(true, forKey: Key.bluetoothShown)
        refreshBluetooth()
    }

    /// The diagnostics answer: posts `diagnosticsOn` to /api/settings. The step is done only when the
    /// controller took the answer (so the setting and the app never disagree).
    func answerDiagnostics(_ consent: Bool) async {
        do {
            try await api.post("/api/settings", json: ["diagnosticsOn": .bool(consent)])
            consentError = nil
            diagnosticsAnswered = true
            defaults.set(true, forKey: Key.diagnosticsAnswered)
        } catch let error as BrewAPIError {
            consentError = error
        } catch {
            consentError = .invalidRequest(String(describing: error))
        }
    }

    /// Forgets everything (settings screen, tests).
    func reset() {
        for key in [Key.safetyAccepted, Key.mode, Key.diagnosticsAnswered, Key.bluetoothShown] {
            defaults.removeObject(forKey: key)
        }
        safetyAccepted = false
        mode = nil
        diagnosticsAnswered = false
        bluetoothShown = false
    }
}
