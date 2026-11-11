import SwiftUI

/// True when XCTest hosts its unit tests in this app: those tests start the controller themselves, on
/// a temporary folder, so the app must not. (A UI test runs the app in its own process, without the
/// variable, so it starts normally.)
private let runningUnitTests = ProcessInfo.processInfo.environment["XCTestConfigurationFilePath"] != nil

/// Starts the in-process controller once. Demo mode (the simulated Lunar, SwitchBot and Breville) until
/// the real screens can switch it; `-bbwReal` as a launch argument selects the real devices.
enum Controller {
    static var demo: Bool { !ProcessInfo.processInfo.arguments.contains("-bbwReal") }
    private static var started = false

    static func startOnce() {
        guard !started else { return }
        started = true
        bbw_ios_prepare(demo ? 1 : 0)
        _ = bbw_host_start()
        if demo && ProcessInfo.processInfo.arguments.contains("-bbwDemoReady") { DispatchQueue.global().async(execute: readyTheDemo) }
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
    }
}

@main
struct BrewControllerApp: App {
    init() {
        if !runningUnitTests { Controller.startOnce() }
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

/// Placeholder until the Brew tab exists: state name and weight at 10 Hz, and a Start button.
struct ContentView: View {
    @State private var live = BbwLive()
    private let timer = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()

    private var stateName: String {
        withUnsafeBytes(of: live.state_name) { raw in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
    }

    var body: some View {
        VStack(spacing: 24) {
            Text(stateName).font(.title).accessibilityIdentifier("stateLabel")
            Text(String(format: "%.1f g", live.weight_g))
                .font(.system(size: 48, weight: .light, design: .rounded))
                .accessibilityIdentifier("weightLabel")
            Button("Start") { _ = bbw_command(Int32(BBW_CMD_START)) }
                .buttonStyle(.borderedProminent)
                .accessibilityIdentifier("startButton")
        }
        .padding()
        .onReceive(timer) { _ in bbw_live(&live) }
        .onAppear { UIApplication.shared.isIdleTimerDisabled = true }
    }
}
