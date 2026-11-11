import XCTest

/// Runs a demo shot on the simulated machine and keeps a screenshot of each stage (they are
/// attachments of the test result; Codemagic exports them as artifacts).
final class DemoShotUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    private func screenshot(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func waitForState(_ app: XCUIApplication, _ name: String, timeout: TimeInterval) {
        let label = app.staticTexts["stateLabel"]
        let predicate = NSPredicate(format: "label == %@", name)
        let result = XCTWaiter().wait(for: [expectation(for: predicate, evaluatedWith: label)], timeout: timeout)
        XCTAssertEqual(result, .completed, "never reached \(name); state is \(label.label)")
    }

    private func openSheetAndClose(_ app: XCUIApplication, button: String, name: String) {
        app.buttons[button].tap()
        XCTAssertTrue(app.buttons["doneButton"].waitForExistence(timeout: 5), "\(name) sheet did not open")
        screenshot(app, name)
        app.buttons["doneButton"].tap()
        XCTAssertTrue(app.buttons["primaryButton"].waitForExistence(timeout: 5))
    }

    func testDemoShot() {
        let app = XCUIApplication()
        app.launch()

        XCTAssertTrue(app.staticTexts["stateLabel"].waitForExistence(timeout: 10))
        waitForState(app, "READY", timeout: 15)
        screenshot(app, "01-ready")

        openSheetAndClose(app, button: "recipeButton", name: "02-recipe")
        openSheetAndClose(app, button: "faultsButton", name: "03-faults")

        let primary = app.buttons["primaryButton"]
        XCTAssertEqual(primary.label, "Start")
        primary.tap()

        waitForState(app, "BREWING", timeout: 30)
        sleep(8)
        screenshot(app, "04-brewing")

        waitForState(app, "SHOT_COMPLETE", timeout: 90)
        screenshot(app, "05-shot-complete")
        XCTAssertTrue(app.otherElements["resultCard"].exists || app.staticTexts["resultCard"].exists)

        openSheetAndClose(app, button: "probeButton", name: "06-probe-log")
        openSheetAndClose(app, button: "devicesButton", name: "07-devices")

        app.buttons["primaryButton"].tap()  // Done: back to READY
        waitForState(app, "READY", timeout: 15)
        screenshot(app, "08-ready-again")
    }
}
