import XCTest

/// Insights, Subscriptions, Discover and Community in demo mode (the simulated Lunar, SwitchBot and
/// Breville; there is no library relay, so the shared lists are empty).
///
/// The tab shell belongs to another thread: a screen is reached by its title, as a tab or as a row of the
/// More tab (`open(_:)` tries both). Every screen's root has an accessibility identifier
/// (insightsScreen, subscriptionsScreen, discoverScreen, communityScreen).
final class DiscoverScreensUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

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

    /// Any element with this accessibility identifier, whatever its type.
    private func element(_ app: XCUIApplication, _ identifier: String) -> XCUIElement {
        app.descendants(matching: .any).matching(identifier: identifier).firstMatch
    }

    private func elements(_ app: XCUIApplication, beginningWith prefix: String) -> XCUIElementQuery {
        app.descendants(matching: .any).matching(NSPredicate(format: "identifier BEGINSWITH %@", prefix))
    }

    /// Opens a screen by its title: a tab, or a row of the More tab.
    private func open(_ app: XCUIApplication, _ title: String, root: String) {
        let tab = app.tabBars.buttons[title]
        if tab.exists {
            tab.tap()
        } else {
            let more = app.tabBars.buttons["More"]
            if more.exists { more.tap() }
            let row = app.buttons[title].exists ? app.buttons[title] : app.staticTexts[title]
            XCTAssertTrue(row.waitForExistence(timeout: 10), "no \(title) on the More tab")
            row.tap()
        }
        XCTAssertTrue(element(app, root).waitForExistence(timeout: 20), "\(title) did not open")
    }

    /// Back to the list the screen was pushed from (no-op when it is a tab).
    private func back(_ app: XCUIApplication) {
        let button = app.navigationBars.buttons.element(boundBy: 0)
        if button.exists && app.tabBars.buttons["More"].exists { button.tap() }
    }

    // MARK: Opening

    func testOpenEachScreen() {
        let app = launch()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 30))

        open(app, "Insights", root: "insightsScreen")
        XCTAssertTrue(element(app, "insightsTaste").waitForExistence(timeout: 20))
        XCTAssertTrue(element(app, "insightsGrind").exists || element(app, "insightsFailed").exists)
        screenshot(app, "01-insights")
        back(app)

        open(app, "Subscriptions", root: "subscriptionsScreen")
        XCTAssertTrue(element(app, "subscriptionsEmpty").waitForExistence(timeout: 20))
        XCTAssertTrue(element(app, "subscriptionsLibrary").exists)
        screenshot(app, "02-subscriptions")
        back(app)

        open(app, "Discover", root: "discoverScreen")
        XCTAssertTrue(element(app, "discoverCoffeeRecommendations").waitForExistence(timeout: 20))
        // Nothing rated yet: the card says how many coffees to rate.
        XCTAssertTrue(element(app, "discoverCoffeeRecommendationsEmpty").exists)
        XCTAssertTrue(element(app, "discoverCoffeeLibrary").exists)
        screenshot(app, "03-discover")
        back(app)

        open(app, "Community", root: "communityScreen")
        XCTAssertTrue(element(app, "communityFeed").waitForExistence(timeout: 20))
        screenshot(app, "04-community")
    }

    // MARK: Insights after a shot

    func testInsightsChartsAfterADemoShot() {
        let app = launch()
        let state = app.staticTexts["stateLabel"]
        XCTAssertTrue(state.waitForExistence(timeout: 30))
        let ready = NSPredicate(format: "label == 'READY'")
        XCTAssertEqual(XCTWaiter().wait(for: [expectation(for: ready, evaluatedWith: state)], timeout: 60), .completed)
        app.buttons["startButton"].tap()
        // The simulated shot stops by weight by itself; the controller answers 409 for the shots while it runs.
        let moved = NSPredicate(format: "label != 'READY'")
        XCTAssertEqual(XCTWaiter().wait(for: [expectation(for: moved, evaluatedWith: state)], timeout: 30), .completed)

        open(app, "Insights", root: "insightsScreen")
        let calendar = element(app, "insightsCalendarChart")
        var found = false
        for _ in 0..<12 {
            if calendar.waitForExistence(timeout: 10) { found = true; break }
            let retry = element(app, "insightsRetry")
            if retry.exists { retry.tap() } else { app.swipeDown() }
        }
        XCTAssertTrue(found, "no calendar chart after the demo shot")
        // The calendar and the dial-in chart have data (the shot is dated and has a target time) ...
        XCTAssertTrue(element(app, "insightsDialInChart").exists)
        // ... the roast and age charts say what they need (the demo shot has no coffee or roast date).
        XCTAssertTrue(element(app, "insightsRoastGrindEmpty").exists)
        XCTAssertTrue(element(app, "insightsAgeEmpty").exists)
        XCTAssertTrue(element(app, "insightsGrindEmpty").exists)
        screenshot(app, "05-insights-charts")

        // The four calendar modes.
        for title in ["Coffee & recipe", "Drift", "Time of day", "Shots"] {
            let mode = app.buttons[title]
            XCTAssertTrue(mode.waitForExistence(timeout: 10), "no \(title) calendar mode")
            mode.tap()
            XCTAssertTrue(element(app, "insightsCalendarChart").waitForExistence(timeout: 10))
        }
    }

    // MARK: Subscriptions

    func testCreateEditAndMarkASubscriptionArrived() {
        let app = launch()
        open(app, "Subscriptions", root: "subscriptionsScreen")
        let add = app.buttons["subscriptionAdd"]
        XCTAssertTrue(add.waitForExistence(timeout: 20))
        add.tap()

        // A supplies subscription: 40 paper filters in stock, 100 more with each delivery.
        let name = app.textFields["subscriptionFormName"]
        XCTAssertTrue(name.waitForExistence(timeout: 10))
        name.tap()
        name.typeText("Test filters")
        app.buttons["subscriptionFormKind"].tap()
        app.buttons["Supplies (filters...)"].tap()
        let item = app.textFields["subscriptionFormItem"]
        XCTAssertTrue(item.waitForExistence(timeout: 10))
        item.tap()
        item.typeText("paper filters")
        let stock = app.textFields["subscriptionFormStock"]
        stock.tap()
        stock.typeText("40")
        let perDelivery = app.textFields["subscriptionFormPerDelivery"]
        perDelivery.tap()
        perDelivery.typeText("100")
        screenshot(app, "06-subscription-form")
        app.buttons["subscriptionFormSave"].tap()

        let card = elements(app, beginningWith: "subscriptionCard.").firstMatch
        XCTAssertTrue(card.waitForExistence(timeout: 20), "no subscription card after saving")
        XCTAssertTrue(app.staticTexts["Test filters"].exists)

        // Edit: add a note.
        elements(app, beginningWith: "subscriptionEdit.").firstMatch.tap()
        let note = app.textFields["subscriptionFormNote"]
        XCTAssertTrue(note.waitForExistence(timeout: 10))
        note.tap()
        note.typeText("2 packs")
        app.buttons["subscriptionFormSave"].tap()
        XCTAssertTrue(app.staticTexts["2 packs"].waitForExistence(timeout: 20), "the edited note is not on the card")

        // Arrived: 100 more filters in stock.
        elements(app, beginningWith: "subscriptionArrived.").firstMatch.tap()
        let confirm = app.alerts.firstMatch
        XCTAssertTrue(confirm.waitForExistence(timeout: 10))
        confirm.buttons["Arrived"].tap()
        let stocked = app.staticTexts.matching(NSPredicate(format: "label BEGINSWITH '140 paper filters'")).firstMatch
        XCTAssertTrue(stocked.waitForExistence(timeout: 20), "stock did not become 140")
        screenshot(app, "07-subscription-arrived")
    }

    // MARK: Community

    func testCommunityFeedIsEmptyWithoutTheLibrary() {
        let app = launch()
        open(app, "Community", root: "communityScreen")
        let empty = element(app, "communityFeedEmpty")
        XCTAssertTrue(empty.waitForExistence(timeout: 20))
        XCTAssertTrue(empty.label.hasPrefix("Nothing in the feed yet"), empty.label)
        // The other shared lists say the same: nothing yet.
        XCTAssertTrue(element(app, "communityTrendingEmpty").exists)
        XCTAssertTrue(element(app, "communityGrindersEmpty").exists)
        XCTAssertTrue(element(app, "communityShareOff").exists || element(app, "communityShareCounts").exists)
        screenshot(app, "08-community-empty")
    }
}
