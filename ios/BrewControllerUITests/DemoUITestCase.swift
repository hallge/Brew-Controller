import XCTest

/// Base of the Brew tab's UI tests: launches the app in demo mode (the simulated Lunar, SwitchBot and
/// Breville; `-bbwDemoReady` pairs the bot and puts the cup on the scale) and finds views by the
/// accessibility identifiers the screens set. Screenshots are attachments of the test result;
/// Codemagic exports them as artifacts.
class DemoUITestCase: XCTestCase {
    var app: XCUIApplication!

    override func setUp() {
        continueAfterFailure = false
    }

    func launch(_ extra: [String] = []) {
        app = XCUIApplication()
        app.launchArguments = ["-bbwDemoReady"] + extra
        app.launch()
    }

    /// Any element with this accessibility identifier.
    func element(_ id: String) -> XCUIElement {
        app.descendants(matching: .any).matching(identifier: id).firstMatch
    }

    func screenshot(_ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    /// Polls until `condition` holds; false when `timeout` passes first.
    @discardableResult
    func poll(_ timeout: TimeInterval, _ condition: () -> Bool) -> Bool {
        let end = Date().addingTimeInterval(timeout)
        while Date() < end {
            if condition() { return true }
            Thread.sleep(forTimeInterval: 0.4)
        }
        return condition()
    }

    /// The home panel with the status READY. A grind question that comes first (NO GRIND SET, new
    /// advice) is answered with its NOT NOW / KEEP key.
    func waitForReadyHome(timeout: TimeInterval = 150, file: StaticString = #filePath, line: UInt = #line) {
        let ready = poll(timeout) {
            let keep = element("grindKeepButton")
            if keep.exists { keep.tap(); return false }
            let status = element("statusLine")
            return status.exists && status.label.hasPrefix("READY")
        }
        let last = element("statusLine").exists ? element("statusLine").label : "no status line"
        XCTAssertTrue(ready, "the home panel never said READY (last: \(last))", file: file, line: line)
    }

    /// Swipes the open screen up until the element can be touched (a card far down the home panel).
    @discardableResult
    func scrollTo(_ id: String, maxSwipes: Int = 10) -> XCUIElement {
        let target = element(id)
        var swipes = 0
        while !(target.exists && target.isHittable) && swipes < maxSwipes {
            app.swipeUp()
            swipes += 1
        }
        return target
    }

    /// Starts a shot from the ready home panel and waits for the brew panel.
    func startShot(file: StaticString = #filePath, line: UInt = #line) {
        let start = element("startButton")
        XCTAssertTrue(start.waitForExistence(timeout: 10), "no Start button", file: file, line: line)
        XCTAssertTrue(start.isEnabled, "Start is disabled while READY", file: file, line: line)
        start.tap()
        XCTAssertTrue(element("shotRing").waitForExistence(timeout: 40), "the brew panel never appeared", file: file, line: line)
    }
}
