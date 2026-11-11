import XCTest

/// The Brew tab, shell and first-run controls added after build 3: the board's Settings from Brew, the
/// last-shot card in full, a manual-style machine's Start, hold-to-repeat on the edit keys, and More
/// (gated rows, Getting started, the first run). More rows are found by identifier, never by label: a
/// bare label also matches buttons on other tabs.
final class BrewGapsUITests: DemoUITestCase {
    func testSettingsSheetOpensWithItsKeys() {
        launch()
        waitForReadyHome()
        scrollTo("settingsButton").tap()
        XCTAssertTrue(element("settingsSheet").waitForExistence(timeout: 10), "the settings sheet")
        XCTAssertTrue(element("settingsLearning").waitForExistence(timeout: 5))
        XCTAssertTrue(element("settingsTare").exists)
        XCTAssertTrue(element("settingsPairBot").exists)
        XCTAssertTrue(element("settingsForgetBot").exists)
        XCTAssertTrue(element("settingsForgetScale").exists)
        XCTAssertTrue(poll(10) { self.element("settingsBotStatus").exists && !self.element("settingsBotStatus").label.isEmpty },
                      "the bot's state is written")
        screenshot("settings-sheet")
        // OFFSET: LEARNING <-> FIXED, and back as it was.
        let learning = element("settingsLearning")
        let before = learning.label
        learning.tap()
        XCTAssertTrue(poll(15) { self.element("settingsLearning").label != before }, "the offset key flips (was \(before))")
        element("settingsLearning").tap()
        XCTAssertTrue(poll(15) { self.element("settingsLearning").label == before }, "and flips back")
        element("settingsDone").tap()
        XCTAssertTrue(poll(10) { !self.element("settingsSheet").exists })
    }

    func testLastShotCardIsThereWithAnEmptyStateOrASummary() {
        launch()
        waitForReadyHome()
        scrollTo("lastShotCard")
        XCTAssertTrue(element("lastShotCard").exists, "the last-shot card")
        // A fresh demo has no shot ("No shot yet"); a used one shows its numbers.
        XCTAssertTrue(poll(20) { self.element("lastShotEmpty").exists || self.element("lastShotWeight").exists },
                      "neither the empty state nor the summary")
        XCTAssertTrue(element("previousAdviceCard").exists, "the advice after the last shot")
        XCTAssertTrue(element("grinderCard").exists, "the grinder card")
    }

    func testLastShotCardAfterAFullShotHasEveryControl() {
        launch()
        waitForReadyHome()
        startShot()
        XCTAssertTrue(element("resultTop").waitForExistence(timeout: 150), "the result panel never appeared")
        if element("readyButton").exists {
            element("readyButton").tap()
        } else if element("countButton").exists {
            element("countButton").tap()
            if element("readyButton").waitForExistence(timeout: 10) { element("readyButton").tap() }
        }
        XCTAssertTrue(poll(30) { self.element("statusLine").exists }, "back on the home panel")
        scrollTo("lastShotCard")
        XCTAssertTrue(poll(30) { self.element("lastShotWeight").exists }, "the summary of the shot just pulled")
        XCTAssertTrue(element("lastShotVerdict").exists, "on target / over / under")
        XCTAssertTrue(element("lastShotDetail").exists, "first drops, pour, when")
        XCTAssertTrue(element("lastShotChart").waitForExistence(timeout: 20) || element("lastShotNoCurve").exists,
                      "the curve, or the note that it is gone")
        XCTAssertTrue(element("grindVotePicker").exists, "Auto / Count / Ignore")
        XCTAssertTrue(element("prepDose").exists && element("prepPurged").exists && element("prepUneven").exists, "preparation")
        XCTAssertTrue(element("lastShotBeanconqueror").exists, "Send to Beanconqueror")
        XCTAssertTrue(element("lastShotOpen").exists, "link to the shot")
        screenshot("last-shot-card")
        // The dose out of range is refused with a message, not sent.
        let dose = scrollTo("prepDose")
        dose.tap()
        dose.typeText("99")
        scrollTo("prepSave").tap()
        XCTAssertTrue(app.alerts["Not saved"].waitForExistence(timeout: 5), "5 to 40 g only")
        app.alerts["Not saved"].buttons["OK"].tap()
        // Open leaves Home for the shot's own screen.
        scrollTo("lastShotOpen").tap()
        XCTAssertTrue(poll(10) { !self.element("lastShotCard").exists }, "the shot's screen opened")
    }

    func testManualMachineStartsFromTheApp() {
        launch(["-bbwDemoManual"])
        // No bot: the board says "pair the bot" only for a machine that needs one; a lever machine does not.
        XCTAssertTrue(poll(150) {
            let keep = self.element("grindKeepButton")
            if keep.exists { keep.tap() }
            return self.element("startButton").exists && self.element("startButton").isEnabled
        }, "Start never became available on a manual machine")
        element("startButton").tap()
        XCTAssertTrue(element("brewPanel").waitForExistence(timeout: 40), "the brew panel")
        // Ready to brew: the key of the phone, in the phone's words.
        XCTAssertTrue(poll(60) { self.element("startButton").exists || self.element("stopButton").exists },
                      "the brew panel offers Start (or the shot is already under way)")
        let phase = element("phaseLabel")
        if phase.exists && element("startButton").exists {
            XCTAssertFalse(phase.label.contains("TOUCH KEY"), "no word of the board's key: \(phase.label)")
        }
        screenshot("manual-start")
    }

    func testEditKeyRepeatsWhenHeld() {
        launch()
        waitForReadyHome()
        let edit = element("editRecipeButton")
        XCTAssertTrue(poll(20) { edit.isEnabled })
        edit.tap()
        let value = element("editValue")
        XCTAssertTrue(value.waitForExistence(timeout: 10))
        element("editField-3").tap()
        let start = value.label
        element("editPlus").tap()
        XCTAssertTrue(poll(5) { value.label != start })
        let one = value.label
        element("editPlus").press(forDuration: 2.0)
        XCTAssertTrue(poll(5) { value.label != one }, "holding + goes on stepping (one tap: \(one))")
        screenshot("edit-held")
        element("editCancel").tap()
    }

    func testMoreHasGettingStartedAndTheFirstRun() {
        launch()
        waitForReadyHome()
        app.tabBars.buttons["More"].tap()
        XCTAssertTrue(element("moreRow-GettingStarted").waitForExistence(timeout: 10), "Getting started row")
        // The demo turns the web page's panels on, so every row is there.
        for name in ["Recipes", "Machine", "Insights", "Subscriptions", "Discover", "Community", "Devices", "Setup", "Logs"] {
            XCTAssertTrue(element("moreRow-\(name)").exists, "More row \(name)")
        }
        element("moreRow-SafetyNotice").tap()
        XCTAssertTrue(element("safetySheet").waitForExistence(timeout: 5), "the safety notice again")
        element("safetyDone").tap()
        XCTAssertTrue(poll(5) { !self.element("safetySheet").exists })
        XCTAssertTrue(element("moreRow-FirstRun").exists, "Show first run again")
        screenshot("more")
    }

    func testGettingStartedCardLeadsToTheGuideWhenThereIsOne() {
        launch()
        waitForReadyHome()
        // The card shows until every step is done; in a demo that has not set up a grinder it is there.
        guard element("startedCard").waitForExistence(timeout: 15) else { return }
        XCTAssertTrue(element("startedProgress").exists)
        element("startedContinue").tap()
        XCTAssertTrue(poll(10) { !self.element("homePanel").exists || !self.element("startedCard").isHittable }, "the guide opened")
    }
}
