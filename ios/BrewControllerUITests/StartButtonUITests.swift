import XCTest

/// Launches the app (demo mode), taps Start and sees the state change. Screenshots are attachments of
/// the test result; Codemagic exports them as artifacts.
final class StartButtonUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    private func screenshot(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    func testStartChangesTheState() {
        let app = XCUIApplication()
        app.launchArguments = ["-bbwDemoReady"]
        app.launch()

        let state = app.staticTexts["stateLabel"]
        XCTAssertTrue(state.waitForExistence(timeout: 10))
        // The app pairs the simulated bot and puts the cup on the scale; the controller then says READY.
        let before = NSPredicate(format: "label == 'READY'")
        XCTAssertEqual(XCTWaiter().wait(for: [expectation(for: before, evaluatedWith: state)], timeout: 60), .completed)
        let first = state.label
        screenshot(app, "01-\(first)")

        app.buttons["startButton"].tap()
        let changed = NSPredicate(format: "label != %@", first)
        let result = XCTWaiter().wait(for: [expectation(for: changed, evaluatedWith: state)], timeout: 30)
        screenshot(app, "02-after-start")
        XCTAssertEqual(result, .completed, "state stayed \(first) after Start")
    }
}
