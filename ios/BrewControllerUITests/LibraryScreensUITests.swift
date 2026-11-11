import XCTest

/// The Shots, Coffees and Recipes screens in demo mode (the simulated Lunar, SwitchBot and Breville): open
/// each one, create and edit a coffee and a bag, create and duplicate a recipe, and open a shot's detail
/// after a demo shot. The tab names are the shell's ("Shots", "Coffees", and Recipes inside "More"); the
/// rest are the accessibility identifiers the screens set. Screenshots are attachments of the result.
final class LibraryScreensUITests: XCTestCase {
    private var app: XCUIApplication!

    override func setUp() {
        continueAfterFailure = false
        app = XCUIApplication()
        app.launchArguments = ["-bbwDemoReady"]
        app.launch()
    }

    // MARK: Helpers

    private func screenshot(_ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    /// The first element, of any kind, whose identifier starts with `prefix`.
    private func element(prefix: String) -> XCUIElement {
        app.descendants(matching: .any).matching(NSPredicate(format: "identifier BEGINSWITH %@", prefix)).firstMatch
    }

    private func element(id: String) -> XCUIElement {
        app.descendants(matching: .any).matching(identifier: id).firstMatch
    }

    private func openTab(_ name: String) {
        let button = app.tabBars.buttons[name]
        XCTAssertTrue(button.waitForExistence(timeout: 30), "no tab named \(name)")
        button.tap()
    }

    /// Recipes is pushed from More.
    private func openRecipes() {
        openTab("More")
        let row = element(id: "moreRow-Recipes")  // a bare "Recipes" label also matches the Brew tab
        XCTAssertTrue(row.waitForExistence(timeout: 10), "More has no Recipes row")
        row.tap()
        XCTAssertTrue(element(id: "recipesList").waitForExistence(timeout: 20))
    }

    /// Scrolls the screen up until the element can be tapped.
    private func reveal(_ element: XCUIElement, file: StaticString = #filePath, line: UInt = #line) {
        XCTAssertTrue(element.waitForExistence(timeout: 15), "missing \(element)", file: file, line: line)
        var tries = 0
        while !element.isHittable && tries < 12 {
            app.swipeUp()
            tries += 1
        }
        XCTAssertTrue(element.isHittable, "\(element) is not reachable", file: file, line: line)
    }

    /// Types `text` into a text field, replacing what it holds.
    private func replaceText(_ field: XCUIElement, with text: String) {
        reveal(field)
        field.tap()
        if let current = field.value as? String, !current.isEmpty, current != field.placeholderValue {
            field.typeText(String(repeating: XCUIKeyboardKey.delete.rawValue, count: current.count))
        }
        field.typeText(text)
    }

    /// Ends the edit in a field with a keyboard that has no return key.
    private func dismissKeyboard() {
        let done = app.keyboards.buttons["Done"].exists ? app.keyboards.buttons["Done"] : app.buttons["Done"]
        if done.exists { done.tap() }
    }

    // MARK: Tests

    func testShotsScreenOpens() {
        openTab("Shots")
        XCTAssertTrue(element(id: "shotsList").waitForExistence(timeout: 30))
        // The summary tiles and the filters are there even with no shots.
        XCTAssertTrue(element(id: "shotsTile-Shots").waitForExistence(timeout: 15))
        XCTAssertTrue(element(id: "shotsFilterShow").exists)
        screenshot("shots-empty-or-listed")
    }

    func testCoffeesScreenCreatesAndEditsACoffeeAndABag() {
        openTab("Coffees")
        XCTAssertTrue(element(id: "coffeesList").waitForExistence(timeout: 30))

        // A new coffee.
        element(id: "coffeeAdd").tap()
        replaceText(app.textFields["coffeeName"], with: "UI Test Coffee")
        replaceText(app.textFields["coffeeRoaster"], with: "Test Roasters")
        dismissKeyboard()
        reveal(element(id: "coffeeSave"))
        element(id: "coffeeSave").tap()
        XCTAssertTrue(app.staticTexts["UI Test Coffee"].waitForExistence(timeout: 20), "the new coffee is not on the shelf")
        screenshot("coffees-new-coffee")

        // Edit it.
        app.staticTexts["UI Test Coffee"].tap()
        replaceText(app.textFields["coffeeName"], with: "UI Test Coffee B")
        replaceText(app.textFields["coffeeOrigin"], with: "Kenya")
        dismissKeyboard()
        // The bag in use: set its roast date (today) and save; the date corrects the bag in use.
        reveal(element(id: "coffeeRoastDate"))
        element(id: "coffeeRoastDate").tap()
        reveal(element(id: "coffeeSave"))
        element(id: "coffeeSave").tap()
        XCTAssertTrue(app.navigationBars["UI Test Coffee B"].waitForExistence(timeout: 20)
                      || app.staticTexts["UI Test Coffee B"].waitForExistence(timeout: 5), "the edit was not saved")

        // A new bag.
        reveal(element(id: "coffeeNewBag"))
        element(id: "coffeeNewBag").tap()
        reveal(element(id: "coffeeBagRoastDate"))
        element(id: "coffeeBagRoastDate").tap()
        reveal(element(id: "coffeeBagStart"))
        element(id: "coffeeBagStart").tap()
        // Starting a bag with today's roast date asks first when the bag in use has the same date.
        let again = app.buttons["Start another bag"]
        if again.waitForExistence(timeout: 3) { again.tap() }
        XCTAssertTrue(element(prefix: "coffeeBag-").waitForExistence(timeout: 20), "no bag listed")
        screenshot("coffees-bag-started")
    }

    func testRecipesScreenCreatesAndDuplicatesARecipe() {
        openRecipes()
        element(id: "recipeNew").tap()
        replaceText(app.textFields["recipeName"], with: "UI Recipe")
        replaceText(app.textFields["recipeDose"], with: "18")
        dismissKeyboard()
        reveal(element(id: "recipeSave"))
        element(id: "recipeSave").tap()
        XCTAssertTrue(app.staticTexts["UI Recipe"].waitForExistence(timeout: 20), "the new recipe is not listed")
        screenshot("recipes-created")

        // Duplicate: the copy is named "UI Recipe 2".
        app.staticTexts["UI Recipe"].tap()
        reveal(element(id: "recipeDuplicate"))
        element(id: "recipeDuplicate").tap()
        XCTAssertTrue(app.staticTexts["UI Recipe 2"].waitForExistence(timeout: 20), "the copy is not listed")
        screenshot("recipes-duplicated")
    }

    func testShotDetailOpensAfterADemoShot() {
        // Open Shots once so its history loads, then pull a demo shot from the Brew tab.
        openTab("Shots")
        XCTAssertTrue(element(id: "shotsList").waitForExistence(timeout: 30))
        let firstTab = app.tabBars.buttons.element(boundBy: 0)
        firstTab.tap()

        let start = app.buttons["startButton"].exists ? app.buttons["startButton"] : app.buttons["Start"]
        XCTAssertTrue(start.waitForExistence(timeout: 90), "no Start button on the first tab")
        // The demo needs a moment to pair the bot and put the cup on the scale.
        let ready = NSPredicate(format: "isEnabled == true")
        XCTAssertEqual(XCTWaiter().wait(for: [expectation(for: ready, evaluatedWith: start)], timeout: 90), .completed)
        start.tap()

        // The simulated shot takes about half a minute; the history reloads when it ends.
        openTab("Shots")
        let row = element(prefix: "shotRow-")
        var found = row.waitForExistence(timeout: 20)
        var tries = 0
        while !found && tries < 12 {
            element(id: "shotsList").swipeDown()
            found = row.waitForExistence(timeout: 10)
            tries += 1
        }
        XCTAssertTrue(found, "no shot in the history after the demo shot")
        screenshot("shots-listed")

        row.tap()
        XCTAssertTrue(element(id: "shotDetail").waitForExistence(timeout: 20), "the shot detail did not open")
        XCTAssertTrue(element(id: "shotVote").exists)
        XCTAssertTrue(element(id: "shotsCurve").waitForExistence(timeout: 20), "no curve")
        screenshot("shot-detail")

        // Rate it: 4 stars.
        reveal(element(id: "shotStar-4"))
        element(id: "shotStar-4").tap()
        XCTAssertTrue(element(id: "shotStar-4").waitForExistence(timeout: 10))

        // Add it to the comparison; the list then offers the comparison.
        reveal(element(id: "shotCompareToggle"))
        element(id: "shotCompareToggle").tap()
        app.navigationBars.buttons.element(boundBy: 0).tap()
        XCTAssertTrue(element(id: "shotsCompareLink").waitForExistence(timeout: 10), "no comparison link after adding a shot")
        element(id: "shotsCompareLink").tap()
        XCTAssertTrue(element(id: "shotsCompareChart").waitForExistence(timeout: 20), "no comparison chart")
        screenshot("shots-compare")
    }

    // MARK: More rows of the parity list

    func testShotsExportAndClearHistoryAsk() {
        openTab("Shots")
        XCTAssertTrue(element(id: "shotsList").waitForExistence(timeout: 30))
        XCTAssertTrue(element(id: "shotsExport").exists)
        // Clear history asks first; cancel it.
        reveal(element(id: "shotsClear"))
        element(id: "shotsClear").tap()
        let confirm = app.buttons["Clear history"]
        XCTAssertTrue(confirm.waitForExistence(timeout: 10), "no confirmation before clearing the history")
        let cancel = app.buttons["Cancel"]
        if cancel.exists { cancel.tap() } else { app.tap() }
    }

    func testCoffeeRatingBrewWithAndCommunity() {
        openTab("Coffees")
        XCTAssertTrue(element(id: "coffeesList").waitForExistence(timeout: 30))
        element(id: "coffeeAdd").tap()
        replaceText(app.textFields["coffeeName"], with: "UI Rated Coffee")
        dismissKeyboard()
        reveal(element(id: "coffeeSave"))
        element(id: "coffeeSave").tap()
        XCTAssertTrue(app.staticTexts["UI Rated Coffee"].waitForExistence(timeout: 20))
        app.staticTexts["UI Rated Coffee"].tap()

        // Share this coffee: the state is shown.
        reveal(element(id: "coffeeSharePill"))

        // How you liked it: 4 stars, a flavour, save, then it is listed; delete it again.
        reveal(element(id: "coffeeRatingStar-4"))
        element(id: "coffeeRatingStar-4").tap()
        reveal(element(id: "coffeeRatingFlavour-0"))
        element(id: "coffeeRatingFlavour-0").tap()
        reveal(element(id: "coffeeRatingSave"))
        element(id: "coffeeRatingSave").tap()
        XCTAssertTrue(element(prefix: "coffeeRating-").waitForExistence(timeout: 20), "the rating is not listed")
        reveal(element(prefix: "coffeeRatingDelete-"))
        element(prefix: "coffeeRatingDelete-").tap()
        let delete = app.buttons["Delete"]
        if delete.waitForExistence(timeout: 5) { delete.tap() }

        // Brew it with: tick the first recipe; the community card then speaks for that pair.
        reveal(element(prefix: "coffeeBrewWith-"))
        element(prefix: "coffeeBrewWith-").tap()
        reveal(element(id: "coffeeCommunityNote"))
        screenshot("coffee-rating-brew-with-community")
    }

    func testCoffeeFinishAndDelete() {
        openTab("Coffees")
        XCTAssertTrue(element(id: "coffeesList").waitForExistence(timeout: 30))
        element(id: "coffeeAdd").tap()
        replaceText(app.textFields["coffeeName"], with: "UI Finish Coffee")
        dismissKeyboard()
        reveal(element(id: "coffeeSave"))
        element(id: "coffeeSave").tap()
        XCTAssertTrue(app.staticTexts["UI Finish Coffee"].waitForExistence(timeout: 20))
        app.staticTexts["UI Finish Coffee"].tap()

        // A bag in use first (a coffee with no bag has nothing to finish).
        reveal(element(id: "coffeeNewBag"))
        element(id: "coffeeNewBag").tap()
        reveal(element(id: "coffeeBagRoastDate"))
        element(id: "coffeeBagRoastDate").tap()
        reveal(element(id: "coffeeBagStart"))
        element(id: "coffeeBagStart").tap()
        let again = app.buttons["Start another bag"]
        if again.waitForExistence(timeout: 3) { again.tap() }

        // Bag finished, with its confirmation: the coffee folds into Finished bags.
        reveal(element(id: "coffeeFinish"))
        element(id: "coffeeFinish").tap()
        let finish = app.buttons["Bag finished"]
        XCTAssertTrue(finish.waitForExistence(timeout: 10), "no confirmation before finishing the bag")
        finish.tap()
        XCTAssertFalse(element(id: "coffeeFinish").waitForExistence(timeout: 5), "the bag is still shown as in use")

        // Delete, with its confirmation: back on the shelf list, the coffee is gone.
        reveal(element(id: "coffeeDelete"))
        element(id: "coffeeDelete").tap()
        let confirmDelete = app.buttons["Delete"]
        XCTAssertTrue(confirmDelete.waitForExistence(timeout: 10), "no confirmation before deleting")
        confirmDelete.tap()
        XCTAssertTrue(element(id: "coffeesList").waitForExistence(timeout: 20))
        XCTAssertFalse(app.staticTexts["UI Finish Coffee"].waitForExistence(timeout: 5), "the coffee is still listed")
    }

    func testCoffeeSearchDirectoryVisualizerAndImportAreReachable() {
        openTab("Coffees")
        XCTAssertTrue(element(id: "coffeesList").waitForExistence(timeout: 30))
        // The search field filters the shelf.
        let search = app.searchFields.firstMatch
        if search.waitForExistence(timeout: 10) {
            search.tap()
            search.typeText("zzzz-no-such-coffee")
            XCTAssertTrue(element(id: "coffeesEmpty").waitForExistence(timeout: 10))
            app.buttons["Cancel"].tap()
        }
        // The directory: nothing read from the shared library yet in demo mode.
        reveal(element(id: "coffeeDirectoryLink"))
        element(id: "coffeeDirectoryLink").tap()
        XCTAssertTrue(element(id: "coffeeDirectory").waitForExistence(timeout: 20))
        XCTAssertTrue(element(id: "coffeeDirectoryEmpty").waitForExistence(timeout: 20))
        app.navigationBars.buttons.element(boundBy: 0).tap()
        // The visualizer.coffee lookup opens its own sheet.
        reveal(element(id: "coffeeVisualizer"))
        element(id: "coffeeVisualizer").tap()
        XCTAssertTrue(element(id: "coffeeVisualizerQuery").waitForExistence(timeout: 10))
        element(id: "coffeeVisualizerClose").tap()
        // The Beanconqueror import opens the Files picker (system UI, not driven here).
        reveal(element(id: "coffeeBeanconquerorImport"))
        XCTAssertTrue(element(id: "coffeeBeanconquerorImport").isHittable)
    }

    func testRecipeFormFieldsUsedWithAndDelete() {
        openRecipes()
        element(id: "recipeNew").tap()
        replaceText(app.textFields["recipeName"], with: "UI Full Recipe")
        // Yield by ratio shows the ratio field.
        reveal(element(id: "recipeYieldMode"))
        app.buttons["Ratio"].tap()
        XCTAssertTrue(element(id: "recipeRatio").waitForExistence(timeout: 10))
        replaceText(app.textFields["recipeRatio"], with: "2.5")
        // Bloom forces a held pre-infusion, which shows its hold time.
        replaceText(app.textFields["recipeBloom"], with: "5")
        dismissKeyboard()
        XCTAssertTrue(element(id: "recipeHold").waitForExistence(timeout: 10))
        // "If the target is never reached": the bot after a set time shows the stop-after field.
        reveal(element(id: "recipeTimeLimit"))
        element(id: "recipeTimeLimit").tap()
        let bot = app.buttons["The Bot, after a set time"]
        if bot.waitForExistence(timeout: 5) { bot.tap() }
        XCTAssertTrue(element(id: "recipeMaxTime").waitForExistence(timeout: 10))
        reveal(element(id: "recipeSave"))
        element(id: "recipeSave").tap()
        XCTAssertTrue(app.staticTexts["UI Full Recipe"].waitForExistence(timeout: 20))

        // Open it: used with, share, delete.
        app.staticTexts["UI Full Recipe"].tap()
        reveal(element(id: "recipeShare"))
        XCTAssertFalse(element(id: "recipeShare").isEnabled, "sharing is enabled without a linked library")
        reveal(element(id: "recipeListAlone"))
        element(id: "recipeListAlone").tap()
        reveal(element(prefix: "recipeUsedWith-"))
        reveal(element(prefix: "recipeResetOffset-"))
        element(prefix: "recipeResetOffset-").tap()
        reveal(element(prefix: "recipeTakeOff-"))
        element(prefix: "recipeTakeOff-").tap()
        reveal(element(id: "recipeDelete"))
        element(id: "recipeDelete").tap()
        let confirm = app.buttons["Delete"]
        XCTAssertTrue(confirm.waitForExistence(timeout: 10), "no confirmation before deleting the recipe")
        confirm.tap()
        XCTAssertTrue(element(id: "recipesList").waitForExistence(timeout: 20))
        XCTAssertFalse(app.staticTexts["UI Full Recipe"].waitForExistence(timeout: 5), "the recipe is still listed")
    }
}
