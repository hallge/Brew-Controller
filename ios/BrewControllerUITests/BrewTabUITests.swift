import XCTest

/// The Brew tab in demo mode: home card, a whole shot to the result, the stop confirm, the recipe
/// picker, the edit panel, the coffee details and a fault. Each test is its own launch.
final class BrewTabUITests: DemoUITestCase {
    func testHomeCardShowsAndTheTabsAreThere() {
        launch()
        waitForReadyHome()
        XCTAssertTrue(element("homeCard").exists, "home card")
        XCTAssertFalse(element("cardName").label.isEmpty, "the card names the coffee or recipe")
        XCTAssertTrue(element("cardYield").exists, "yield line")
        XCTAssertTrue(element("scaleLink").exists && element("botLink").exists, "scale and bot dots")
        XCTAssertTrue(element("startButton").isEnabled)
        screenshot("01-home")

        for name in ["Brew", "Shots", "Coffees", "Grinder", "More"] {
            XCTAssertTrue(app.tabBars.buttons[name].exists, "tab \(name)")
        }
        app.tabBars.buttons["Shots"].tap()
        XCTAssertTrue(app.staticTexts["Coming soon"].waitForExistence(timeout: 5))
        app.tabBars.buttons["More"].tap()
        for name in ["Recipes", "Machine", "Insights", "Subscriptions", "Discover", "Community", "Devices", "Setup", "Logs"] {
            XCTAssertTrue(element("moreRow-\(name)").exists, "More row \(name)")
        }
        app.tabBars.buttons["Brew"].tap()
        XCTAssertTrue(element("homeCard").waitForExistence(timeout: 5))
    }

    func testCoffeeDetailsOpenAndClose() {
        launch()
        waitForReadyHome()
        element("coffeeButton").tap()
        XCTAssertTrue(element("coffeeTitle").waitForExistence(timeout: 5), "coffee details")
        screenshot("coffee-details")
        element("coffeeBack").tap()
        XCTAssertTrue(element("homeCard").waitForExistence(timeout: 5))
    }

    func testFullDemoShotReachesTheResult() {
        launch()
        waitForReadyHome()
        startShot()
        screenshot("02-brew")
        XCTAssertTrue(element("phaseLabel").exists && element("weightLabel").exists && element("timeLabel").exists
                      && element("targetLabel").exists && element("flowLabel").exists, "ring, phase, weight, time, target, flow")
        // The demo shot runs for tens of seconds and is stopped at the target weight.
        XCTAssertTrue(element("resultTop").waitForExistence(timeout: 150), "the result panel never appeared")
        screenshot("03-result")
        XCTAssertTrue(element("resultWeight").exists, "final weight")
        XCTAssertTrue(element("resultTimes").exists, "times")
        // READY, READY + GRINDER, or the two vote keys when the shot is put to the owner.
        let ready = element("readyButton")
        if ready.exists {
            ready.tap()
        } else {
            XCTAssertTrue(element("countButton").exists && element("ignoreButton").exists, "a way out of the result")
            element("countButton").tap()
            XCTAssertTrue(ready.waitForExistence(timeout: 10) || element("statusLine").waitForExistence(timeout: 10))
            if ready.exists { ready.tap() }
        }
        XCTAssertTrue(element("statusLine").waitForExistence(timeout: 20) || element("grindKeepButton").exists, "back to the home panel")
    }

    func testStopAsksForConfirmation() {
        launch()
        waitForReadyHome()
        startShot()
        // Before the pump runs the key says CANCEL (no question); once it runs it says STOP.
        let stop = element("stopButton")
        let seen = poll(90) { stop.exists && stop.label == "STOP" }
        XCTAssertTrue(seen, "the STOP key never appeared (last: \(stop.exists ? stop.label : "none"))")
        stop.tap()
        let alert = app.alerts["STOP THE SHOT?"]
        XCTAssertTrue(alert.waitForExistence(timeout: 5), "stop asks first")
        screenshot("04-stop-confirm")
        alert.buttons["KEEP BREWING"].firstMatch.tap()
        XCTAssertTrue(poll(5) { !alert.exists })
        if stop.exists && stop.label == "STOP" {
            stop.tap()
            XCTAssertTrue(alert.waitForExistence(timeout: 5))
            alert.buttons["STOP"].tap()
        }
        // The shot ends: the result panel, a fault, or already back home.
        let ended = poll(90) { self.element("resultTop").exists || self.element("faultTitle").exists || self.element("statusLine").exists }
        XCTAssertTrue(ended, "the shot never ended after Stop")
        screenshot("05-after-stop")
    }

    func testRecipePickerOpensAndSelects() {
        launch()
        waitForReadyHome()
        element("homeCard").tap()
        let row = element("recipeRow-0")
        XCTAssertTrue(row.waitForExistence(timeout: 10), "the recipe list")
        screenshot("06-recipes")
        row.tap()  // the entry in use or another: either way the list closes and home is back
        let back = poll(30) { self.element("homeCard").exists || self.element("grindKeepButton").exists }
        XCTAssertTrue(back, "back on the home panel")
    }

    func testEditPanelStepsAndCancels() {
        launch()
        waitForReadyHome()
        let edit = element("editRecipeButton")
        XCTAssertTrue(poll(20) { edit.isEnabled }, "Edit recipe is enabled once the library is loaded")
        edit.tap()
        let value = element("editValue")
        XCTAssertTrue(value.waitForExistence(timeout: 10), "the edit panel")
        // Brew time: one tap on + steps by 0.5 s.
        element("editField-3").tap()
        let before = value.label
        element("editPlus").tap()
        XCTAssertTrue(poll(5) { value.label != before }, "+ changes the value (\(before))")
        screenshot("07-edit")
        element("editCancel").tap()
        XCTAssertTrue(poll(10) { !value.exists }, "Cancel closes the panel")

        // And SAVE writes it back: the panel closes when the controller took it.
        edit.tap()
        XCTAssertTrue(value.waitForExistence(timeout: 10))
        element("editField-3").tap()
        element("editPlus").tap()
        element("editSave").tap()
        XCTAssertTrue(poll(30) { !value.exists }, "SAVE closes the panel")
    }

    func testFaultShowsItsTitleAndMessage() {
        launch(["-bbwDemoFault"])
        waitForReadyHome()
        element("startButton").tap()
        // The simulated bot ignores the start: START NOT CONFIRMED, with PRESS BUTTON.
        let title = element("faultTitle")
        XCTAssertTrue(title.waitForExistence(timeout: 120), "no fault panel")
        XCTAssertFalse(title.label.isEmpty)
        XCTAssertTrue(element("faultMessage").exists)
        screenshot("08-fault")
        element("faultOkButton").tap()
        XCTAssertTrue(poll(30) { self.element("statusLine").exists || self.element("grindKeepButton").exists }, "OK clears the fault")
    }
}
