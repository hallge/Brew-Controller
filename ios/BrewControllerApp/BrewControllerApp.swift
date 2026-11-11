import SwiftUI

/// True when XCTest hosts its unit tests in this app: those tests start the controller themselves, on
/// a temporary folder, so the app must not. (A UI test runs the app in its own process, without the
/// variable, so it starts normally.)
private let runningUnitTests = ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] != nil

/// Starts the in-process controller once. Demo mode (the simulated Lunar, SwitchBot and Breville) unless
/// the owner chose real devices at the first run (stored by `Onboarding`) or `-bbwReal` is a launch
/// argument. `-bbwDemoReady` and `-bbwSkipOnboarding` (UI tests) skip the first-run screens.
enum Controller {
    static var demo: Bool {
        if ProcessInfo.processInfo.arguments.contains("-bbwReal") { return false }
        return UserDefaults.standard.string(forKey: "bbw.onboarding.mode") != "real"
    }

    static var skipsOnboarding: Bool {
        let arguments = ProcessInfo.processInfo.arguments
        return arguments.contains("-bbwDemoReady") || arguments.contains("-bbwSkipOnboarding")
    }

    private static var started = false

    /// Starts the controller, then the live model and the lifecycle rules that read it. Once only; never
    /// in a unit-test run (those start the controller themselves, on a temporary folder).
    @MainActor
    static func startOnce() {
        guard !started, !runningUnitTests else { return }
        started = true
        bbw_ios_prepare(demo ? 1 : 0)
        _ = bbw_host_start()
        if demo && ProcessInfo.processInfo.arguments.contains("-bbwDemoReady") { DispatchQueue.global().async(execute: readyTheDemo) }
        LiveModel.shared.start()
        AppLifecycle.shared.start()
    }

    /// For the UI test: pair the simulated bot, pick the Breville profile and put the cup on the scale,
    /// as the unit test does through the same web API.
    private static func readyTheDemo() {
        func post(_ path: String, _ json: String) {
            let bytes = Array(json.utf8)
            var response = BbwResponse()
            let rc = bytes.withUnsafeBytes { bbw_request("POST", path, $0.baseAddress, bytes.count, "application/json", 60000, &response) }
            if rc == 0 { bbw_response_free(&response) }
        }
        post("/api/pair", #"{"kind":"bot","address":"e5:8a:3c:11:9d:40","type":1}"#)
        Thread.sleep(forTimeInterval: 1)  // the first request after start-up can answer 409
        post("/api/machine", #"{"builtin":1}"#)
        _ = bbw_demo_set("machine", "cupOnScale", "0")
        Thread.sleep(forTimeInterval: 3)
        _ = bbw_demo_set("machine", "cupOnScale", "1")
        // `-bbwDemoFault`: the simulated bot ignores every command, so Start ends in a fault (START NOT
        // CONFIRMED, with PRESS BUTTON) for the UI test.
        if ProcessInfo.processInfo.arguments.contains("-bbwDemoFault") {
            _ = bbw_demo_set("bot", "ignoreCommands", "99")
        }
    }
}

@main
struct BrewControllerApp: App {
    var body: some Scene {
        WindowGroup {
            RootView()
        }
    }
}
