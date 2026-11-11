import XCTest

/// The Grinder, Machine, Devices, Setup and Logs screens, in demo mode (the app starts with the simulated
/// Lunar, SwitchBot and Breville). Following StartButtonUITests: screenshots are attachments of the result.
///
/// How a test reaches a screen: the Grinder is a tab; the others are reached through the tab shell's
/// More list. `open(_:)` taps the tab with the screen's name, or More and then the row with that name, so
/// it works with whichever of the two the shell uses. Each screen's root view carries an accessibility
/// identifier ("grinderScreen", "machineScreen", "devicesScreen", "setupScreen", "logsScreen").
final class HardwareScreensUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    // MARK: Helpers

    private func launch() -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-bbwDemoReady"]
        app.launch()
        return app
    }

    private func screenshot(_ app: XCUIApplication, _ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func element(_ app: XCUIApplication, _ id: String) -> XCUIElement {
        app.descendants(matching: .any).matching(identifier: id).firstMatch
    }

    /// Opens a screen by its name through a tab, or through More.
    private func open(_ app: XCUIApplication, _ name: String, root: String) {
        if element(app, root).exists { return }
        let tab = app.tabBars.buttons[name]
        if tab.waitForExistence(timeout: 15) {
            tab.tap()
        } else {
            let more = app.tabBars.buttons["More"]
            XCTAssertTrue(more.waitForExistence(timeout: 15), "no tab named \(name) and no More tab")
            more.tap()
            // By the row's identifier: a label such as "Machine" also names buttons on other tabs.
            let back = app.navigationBars.buttons.element(boundBy: 0)
            for _ in 0..<3 where !element(app, "moreTab").exists && back.exists { back.tap() }
            let row = element(app, "moreRow-\(name)")
            XCTAssertTrue(row.waitForExistence(timeout: 10), "no row named \(name) in More")
            row.tap()
        }
        XCTAssertTrue(element(app, root).waitForExistence(timeout: 15), "\(name) did not open")
    }

    private func waitFor(_ element: XCUIElement, _ predicate: String, timeout: TimeInterval = 30) -> Bool {
        let expectation = XCTNSPredicateExpectation(predicate: NSPredicate(format: predicate), object: element)
        return XCTWaiter().wait(for: [expectation], timeout: timeout) == .completed
    }

    // MARK: Tests

    func testOpenEachScreen() {
        let app = launch()
        for (name, root) in [("Grinder", "grinderScreen"), ("Machine", "machineScreen"), ("Devices", "devicesScreen"),
                             ("Setup", "setupScreen"), ("Logs", "logsScreen")] {
            open(app, name, root: root)
            screenshot(app, "open-\(name)")
        }
    }

    func testChangeTheme() {
        let app = launch()
        open(app, "Setup", root: "setupScreen")
        let light = element(app, "setupTheme_1")
        XCTAssertTrue(light.waitForExistence(timeout: 15))
        light.tap()
        XCTAssertTrue(waitFor(light, "value == 'selected'"), "Light was not marked selected")
        screenshot(app, "theme-light")
        let dark = element(app, "setupTheme_0")
        dark.tap()
        XCTAssertTrue(waitFor(dark, "value == 'selected'"), "Dark was not marked selected")
    }

    func testSetTheGrinderSetting() {
        let app = launch()
        open(app, "Grinder", root: "grinderScreen")
        let field = element(app, "grinderSettingField")
        XCTAssertTrue(field.waitForExistence(timeout: 15))
        // A different number each run, so a value kept from the last run cannot pass for this one.
        let setting = String(200 + Int(Date().timeIntervalSince1970) % 90)
        field.tap()
        field.typeText(setting)
        element(app, "grinderSetButton").tap()
        let now = element(app, "grinderSettingNow")
        XCTAssertTrue(waitFor(now, "label == '\(setting)'"), "the setting stayed \(now.label)")
        screenshot(app, "grinder-set-\(setting)")
    }

    func testToggleAMachineOption() {
        let app = launch()
        open(app, "Machine", root: "machineScreen")
        let option = app.switches["machineKeepPowerBot"]
        for _ in 0..<5 where !option.exists { app.swipeUp() }
        XCTAssertTrue(option.waitForExistence(timeout: 15))
        let before = option.value as? String ?? ""
        option.tap()
        XCTAssertTrue(waitFor(option, "value != '\(before)'"), "the option did not change")
        let changed = option.value as? String ?? ""
        screenshot(app, "machine-option-\(changed)")
        // Back to how it was.
        option.tap()
        XCTAssertTrue(waitFor(option, "value == '\(before)'"))
    }

    func testDevicesShowThePairedScaleAndBot() {
        let app = launch()
        open(app, "Devices", root: "devicesScreen")
        let scale = element(app, "devicesScaleState")
        XCTAssertTrue(scale.waitForExistence(timeout: 15))
        XCTAssertTrue(waitFor(scale, "label CONTAINS 'connected'", timeout: 60), "the scale shows as \(scale.label)")
        let bot = element(app, "devicesBotState")
        XCTAssertTrue(bot.waitForExistence(timeout: 15))
        XCTAssertTrue(waitFor(bot, "label != 'not paired'", timeout: 60), "the bot shows as \(bot.label)")
        // The devices in range are listed with role buttons.
        let seen = app.descendants(matching: .any).matching(NSPredicate(format: "identifier BEGINSWITH 'devicesSeen_'"))
        XCTAssertTrue(seen.firstMatch.waitForExistence(timeout: 30), "nothing in the seen list")
        screenshot(app, "devices")
    }

    func testChangeTheLogLevel() {
        let app = launch()
        open(app, "Logs", root: "logsScreen")
        let level = element(app, "logsLevel")
        XCTAssertTrue(level.waitForExistence(timeout: 15))
        level.tap()
        let warnings = app.buttons["Warnings and errors"]
        XCTAssertTrue(warnings.waitForExistence(timeout: 10))
        warnings.tap()
        XCTAssertTrue(element(app, "logsNotice").waitForExistence(timeout: 15), "the level was not saved")
        screenshot(app, "logs-level-warnings")
        // Back to Normal.
        level.tap()
        let normal = app.buttons["Normal: every event"]
        if normal.waitForExistence(timeout: 10) { normal.tap() }
    }
}
