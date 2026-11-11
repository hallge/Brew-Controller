// The models and the pure helpers of the Shots, Coffees and Recipes screens, against JSON captured from the
// real routes (Fixtures/coffee_*.json, recipe_*.json: written by `test/hostapp/run.sh --dump`; the
// directory, community-found and pending ones are written by hand from the handlers in Web.cpp).
import XCTest
@testable import BrewController

private func fixture(_ name: String, file: StaticString = #filePath) throws -> Data {
    let url = URL(fileURLWithPath: "\(file)").deletingLastPathComponent()
        .appendingPathComponent("Fixtures").appendingPathComponent(name + ".json")
    return try Data(contentsOf: url)
}

final class LibraryScreensModelTests: XCTestCase {
    // MARK: Coffees

    func testRatings() throws {
        let ratings = try BrewAPI.decode(CoffeeRatingsModel.self, from: fixture("coffee_ratings"))
        XCTAssertFalse(ratings.named)
        XCTAssertEqual(ratings.drinkers.count, 8)
        XCTAssertTrue(ratings.people.isEmpty)
        XCTAssertEqual(ratings.attrs.count, 5)
        XCTAssertEqual(ratings.families.count, 8)
        XCTAssertEqual(ratings.ratings.count, 1)
        let rating = try XCTUnwrap(ratings.ratings.first)
        XCTAssertEqual(rating.stars, 4)
        XCTAssertEqual(rating.attr, [3, 2, 3, 1, 4])
        XCTAssertEqual(rating.flavours, 5)
        XCTAssertEqual(rating.share, "local")
        XCTAssertEqual(ratings.rating(coffee: rating.coffee, bag: rating.bag, drinker: 0)?.id, rating.id)
        XCTAssertNil(ratings.rating(coffee: rating.coffee, bag: rating.bag, drinker: 3))
        XCTAssertEqual(ratings.drinkerName(0), "Household")
        XCTAssertEqual(ratings.drinkerName(2), "Drinker 2")
    }

    func testRatingsKeepFiveAttributes() throws {
        let json = #"{"named":true,"drinkers":["Ann","","Bo"],"ratings":[{"id":7,"coffee":1,"attr":[1,2]}]}"#
        let ratings = try BrewAPI.decode(CoffeeRatingsModel.self, from: Data(json.utf8))
        XCTAssertEqual(ratings.people.map(\.name), ["Ann", "Bo"])
        XCTAssertEqual(ratings.people.map(\.id), [1, 3])
        XCTAssertEqual(ratings.ratings[0].attr, [1, 2, 0, 0, 0])
        XCTAssertEqual(ratings.drinkerName(3), "Bo")
    }

    func testCommunityNotFound() throws {
        let model = try BrewAPI.decode(CoffeeCommunityModel.self, from: fixture("coffee_community"))
        XCTAssertFalse(model.reachable)
        XCTAssertFalse(model.found)
        XCTAssertEqual(model.start.basis, "none")
        XCTAssertFalse(model.similar.found)
        XCTAssertEqual(model.myAgeDays, -1)
    }

    func testCommunityFound() throws {
        let model = try BrewAPI.decode(CoffeeCommunityModel.self, from: fixture("coffee_community_found"))
        XCTAssertTrue(model.found)
        XCTAssertEqual(model.contributors, 7)
        XCTAssertEqual(model.dialins, 12)
        XCTAssertEqual(model.timeS, 28.5, accuracy: 0.001)
        XCTAssertEqual(model.recipeMatch, "near")
        XCTAssertEqual(model.start.basis, "fitted")
        XCTAssertEqual(model.start.setting, 225)
        XCTAssertEqual(model.start.ageShift, -5)
        XCTAssertTrue(model.similar.found)
        XCTAssertEqual(model.similar.fromCoffee, "Idjwi Island")
        XCTAssertEqual(model.similar.basis, "nearest")
        XCTAssertTrue(model.ageRateKnown)
    }

    func testDirectory() throws {
        let page = try BrewAPI.decode(CoffeeDirectoryModel.self, from: fixture("coffee_directory"))
        XCTAssertEqual(page.coffees.count, 2)
        XCTAssertEqual(page.matched, 2)
        XCTAssertEqual(page.pageSize, 20)
        XCTAssertEqual(page.pageCount, 1)
        XCTAssertEqual(page.facets?.origin["Ethiopia"], 1)
        XCTAssertEqual(page.facets?.roastLevel["light"], 1)
        let guji = page.coffees[0]
        XCTAssertEqual(guji.id, "gc-guji-natural")
        XCTAssertEqual(guji.stars, 4.5, accuracy: 0.001)
        XCTAssertEqual(guji.ratings, 3)
        XCTAssertEqual(guji.altitudeM, 2100)
        XCTAssertEqual(guji.roastLevelNumber, 1)
        XCTAssertFalse(guji.onShelf)
        XCTAssertTrue(page.coffees[1].onShelf)
        XCTAssertEqual(guji.summary, "Demo Roasters · Ethiopia · Natural")

        // What the controller answers before the directory has been read: no pages, no facets.
        let empty = try BrewAPI.decode(CoffeeDirectoryModel.self, from: fixture("coffee_directory_empty"))
        XCTAssertEqual(empty.keptPages, 0)
        XCTAssertTrue(empty.coffees.isEmpty)
        XCTAssertNil(empty.facets)
    }

    func testSharedLibrary() throws {
        let shared = try BrewAPI.decode(CoffeeSharedLibraryModel.self, from: fixture("coffee_community_coffees"))
        XCTAssertEqual(shared.coffees.count, 1)
        XCTAssertEqual(shared.coffees[0].producer, "Smallholders")
        // The adopt body carries the library id and no dates.
        let draft = CoffeeDraft(shared.coffees[0])
        XCTAssertEqual(draft.name, "Guji Natural")
        XCTAssertEqual(draft.roastLevel, 1)
        XCTAssertEqual(draft.roastDate, 0)
    }

    func testShareStatus() throws {
        let share = try BrewAPI.decode(CoffeeShareModel.self, from: fixture("coffee_share"))
        XCTAssertTrue(share.keyReady)
        XCTAssertTrue(share.toLibrary)
        XCTAssertFalse(share.canSend)
        XCTAssertEqual(share.waiting, 0)
    }

    func testCoffeeDraftBody() throws {
        var draft = CoffeeDraft()
        draft.name = String(repeating: "x", count: 40)
        draft.notes = "blueberry\n\"cocoa\""
        draft.altitudeM = 9000
        draft.roastLevel = 9
        draft.roastDate = 20261101
        let body = draft.body(id: 5)
        XCTAssertEqual(body["id"], .int(5))
        XCTAssertEqual(body["name"], .string(String(repeating: "x", count: 23)))
        XCTAssertEqual(body["notes"], .string("blueberry  cocoa"))
        XCTAssertEqual(body["altitudeM"], .int(4000))
        XCTAssertEqual(body["roastLevel"], .int(5))
        XCTAssertEqual(body["roastDate"], .int(20261101))
        XCTAssertEqual(body["frozenDate"], .int(0))
    }

    func testCoffeeDraftFromLibrary() throws {
        let library = try BrewAPI.decode(LibraryModel.self, from: fixture("recipe_library"))
        let coffee = try XCTUnwrap(library.coffees.first)
        let draft = CoffeeDraft(coffee)
        XCTAssertEqual(draft.name, "Guji Natural")
        XCTAssertEqual(draft.altitudeM, 2100)
        XCTAssertEqual(draft.roastDate, 20261101)
        XCTAssertEqual(CoffeeRoast.label(draft.roastLevel), "light")
        XCTAssertEqual(CoffeeRoast.label(0), "not set")
    }

    func testDates() {
        XCTAssertEqual(CoffeeDates.ymd(CoffeeDates.date(20261101)!), 20261101)
        XCTAssertNil(CoffeeDates.date(0))
        XCTAssertEqual(CoffeeDates.ymd(Date()), CoffeeDates.today())
        XCTAssertNil(CoffeeDates.daysSince(0))
        XCTAssertEqual(CoffeeDates.daysSince(CoffeeDates.today()), 0)
    }

    func testShareWords() {
        XCTAssertEqual(CoffeeShareText.state("local"), "on this controller only")
        XCTAssertEqual(CoffeeShareText.state("refused"), "refused by the library")
        XCTAssertEqual(RecipeShareText.state("published"), "published in the recipe book")
    }

    // MARK: Beanconqueror and visualizer.coffee

    func testBeanconquerorImport() throws {
        let json = """
        {"BEANS":[
          {"name":"Guji","roaster":"Demo","roast":"CITY_ROAST","roastingDate":"2026-11-01T00:00:00.000Z",
           "openDate":"2026-11-08T10:00:00.000Z","weight":250,"aromatics":"jasmine, peach","finished":false,
           "frozenDate":"2026-11-02T00:00:00.000Z","unfrozenDate":"",
           "bean_information":[{"country":"Ethiopia","region":"Guji","farmer":"Smallholders","variety":"Heirloom",
             "processing":"Natural","elevation":"2,100 masl"}]},
          {"name":"Old one","roaster":"X","finished":true,"bean_information":[]},
          {"name":"","roaster":"Nameless"}
        ]}
        """
        let all = try XCTUnwrap(CoffeeBeanconqueror.parse(Data(json.utf8), includeFinished: true))
        XCTAssertEqual(all.total, 3)
        XCTAssertEqual(all.beans.count, 2)
        XCTAssertEqual(all.skipped, ["a bean with no name"])
        let withoutFinished = try XCTUnwrap(CoffeeBeanconqueror.parse(Data(json.utf8), includeFinished: false))
        XCTAssertEqual(withoutFinished.beans.count, 1)
        XCTAssertEqual(withoutFinished.skipped.count, 2)

        let bean = withoutFinished.beans[0]
        XCTAssertEqual(bean.coffee.name, "Guji")
        XCTAssertEqual(bean.coffee.origin, "Ethiopia")
        XCTAssertEqual(bean.coffee.producer, "Smallholders")
        XCTAssertEqual(bean.coffee.altitudeM, 2100)
        XCTAssertEqual(bean.coffee.roastLevel, 3)
        XCTAssertEqual(bean.coffee.roastDate, 20261101)
        XCTAssertEqual(bean.openedDate, 20261108)
        XCTAssertEqual(bean.frozenDate, 20261102)
        XCTAssertEqual(bean.thawDate, 0)
        XCTAssertEqual(bean.weightG, 250)
        XCTAssertTrue(bean.hasBagDetails)
        XCTAssertNil(CoffeeBeanconqueror.parse(Data("not json".utf8), includeFinished: false))
    }

    func testBeanconquerorHelpers() {
        XCTAssertEqual(CoffeeBeanconqueror.altitude("1800-2000 m"), 1800)
        XCTAssertEqual(CoffeeBeanconqueror.altitude("9000"), 0)
        XCTAssertEqual(CoffeeBeanconqueror.altitude("high"), 0)
        XCTAssertEqual(CoffeeBeanconqueror.ymd("2026-11-01T00:00:00Z"), 20261101)
        XCTAssertEqual(CoffeeBeanconqueror.ymd(""), 0)
        XCTAssertEqual(CoffeeBeanconqueror.isoDate(20261101), "2026-11-01")
        XCTAssertEqual(CoffeeVisualizer.roastLevel("Medium-Dark"), 4)
        XCTAssertEqual(CoffeeVisualizer.roastLevel("light"), 1)
        XCTAssertEqual(CoffeeVisualizer.roastLevel("Medium"), 3)
        XCTAssertEqual(CoffeeVisualizer.roastLevel(""), 0)
    }

    func testGzipAndLink() throws {
        // CRC-32 of "123456789" is the standard check value.
        XCTAssertEqual(CoffeeBeanconqueror.crc32(Data("123456789".utf8)), 0xCBF4_3926)
        let original = Data(String(repeating: "espresso ", count: 50).utf8)
        let zipped = try XCTUnwrap(CoffeeBeanconqueror.gzip(original))
        XCTAssertEqual(Array(zipped.prefix(3)), [0x1F, 0x8B, 0x08])
        let body = zipped.subdata(in: 10..<(zipped.count - 8))
        let inflated = try (body as NSData).decompressed(using: .zlib) as Data
        XCTAssertEqual(inflated, original)
        XCTAssertFalse(CoffeeBeanconqueror.base64URL(Data([0xFB, 0xFF, 0xFE])).contains { "+/=".contains($0) })

        let shot = try shotFixture()
        let data = try BrewAPI.decode(ShotDataModel.self, from: fixture("shot_data"))
        let url = try XCTUnwrap(CoffeeBeanconqueror.link(shot: shot, data: data, coffee: nil, grinderModel: "Test grinder",
                                                         grindText: "12", now: nil))
        XCTAssertTrue(url.absoluteString.hasPrefix("beanconqueror://ADD_BREW?len="))
        XCTAssertTrue(url.absoluteString.contains("&shareBrew0="))
        let envelope = CoffeeBeanconqueror.envelope(shot: shot, data: data, coffee: nil, grinderModel: "Test grinder",
                                                    grindText: "12", now: nil)
        let brew = try XCTUnwrap(envelope["brew"] as? [String: Any])
        XCTAssertEqual(brew["preparationMethod"] as? String, "Espresso")
        XCTAssertEqual(brew["grindSize"] as? String, "12")
        XCTAssertNotNil(envelope["flow"])
    }

    // MARK: Recipes

    func testRecipeDraft() throws {
        let library = try BrewAPI.decode(LibraryModel.self, from: fixture("recipe_library"))
        let recipe = try XCTUnwrap(library.recipes.first)
        let draft = RecipeDraft(recipe)
        XCTAssertEqual(draft.name, "Double")
        XCTAssertEqual(draft.doseG, 18)
        XCTAssertEqual(draft.effectiveTargetG, 36)

        // Ratio mode: the target is dose times ratio, to a tenth.
        var ratio = RecipeDraft()
        ratio.name = "Ratio"
        ratio.doseG = 18.5
        ratio.useRatio = true
        ratio.ratio = 2.15
        XCTAssertEqual(ratio.effectiveTargetG, 39.8, accuracy: 0.001)
        XCTAssertEqual(ratio.body(id: 0)["targetWeightG"], .double(39.8))
        XCTAssertEqual(ratio.body(id: 0)["useRatio"], .bool(true))

        // A bloom pause forces a held pre-infusion.
        var bloom = RecipeDraft()
        bloom.name = "Bloom"
        bloom.bloomPauseS = 8
        XCTAssertEqual(bloom.effectivePreinfusion, 1)
        XCTAssertEqual(bloom.body(id: 3)["preinfusion"], .int(1))
        XCTAssertEqual(bloom.body(id: 3)["id"], .int(3))

        // Out-of-range numbers are clamped to the form's limits, the name to 23 characters.
        var wild = RecipeDraft()
        wild.name = String(repeating: "n", count: 30)
        wild.doseG = 99
        wild.targetTimeS = 1
        wild.maxShotTimeS = 500
        let clamped = wild.clamped()
        XCTAssertEqual(clamped.name.count, 23)
        XCTAssertEqual(clamped.doseG, 30)
        XCTAssertEqual(clamped.targetTimeS, 5)
        XCTAssertEqual(clamped.maxShotTimeS, 90)
    }

    func testRecipePending() throws {
        let pending = try BrewAPI.decode(RecipePendingModel.self, from: fixture("recipe_pending"))
        XCTAssertTrue(pending.linked)
        XCTAssertEqual(pending.entry(for: 1)?.state, "review")
        XCTAssertEqual(pending.entry(for: 1)?.note, "looks good so far")
        XCTAssertEqual(pending.entry(for: 2)?.state, "declined")
        XCTAssertNil(pending.entry(for: 9))
        let unlinked = try BrewAPI.decode(RecipePendingModel.self, from: fixture("coffee_library_pending"))
        XCTAssertFalse(unlinked.linked)
        XCTAssertTrue(unlinked.recipes.isEmpty)
    }

    // MARK: Shots

    private func shotFixture() throws -> ShotModel {
        let shots = try BrewAPI.decode([ShotModel].self, from: fixture("shots_result"))
        return try XCTUnwrap(shots.first)
    }

    func testShotWords() throws {
        var shot = try shotFixture()
        XCTAssertTrue(ShotsText.byWeight(shot))
        shot.endReason = "weight_predicted"
        XCTAssertTrue(ShotsText.byWeight(shot))
        XCTAssertEqual(ShotsText.endReason(shot), "weight predicted")
        shot.endReason = "timeout"
        XCTAssertFalse(ShotsText.byWeight(shot))

        // Off target only counts for a shot stopped by weight with a target time.
        shot.endReason = "weight"
        shot.targetTimeS = 28
        shot.shotTimeS = 29
        shot.timeBasis = "total"
        XCTAssertFalse(ShotsText.late(shot, tolerance: 2))
        shot.shotTimeS = 33
        XCTAssertTrue(ShotsText.late(shot, tolerance: 2))
        shot.timeBasis = "pour"
        shot.pourTimeS = 28.5
        XCTAssertFalse(ShotsText.late(shot, tolerance: 2))
        shot.endReason = "user"
        shot.pourTimeS = 40
        XCTAssertFalse(ShotsText.late(shot, tolerance: 2))

        shot.stopTargetG = 0
        shot.targetWeightG = 36
        XCTAssertEqual(ShotsText.stopTarget(shot), 36)
        shot.stopTargetG = 34.5
        XCTAssertEqual(ShotsText.stopTarget(shot), 34.5)
        XCTAssertEqual(ShotsText.stars(3), "★★★")
        XCTAssertEqual(ShotsText.tastes[2], "balanced")
    }

    func testShotsGrindParse() {
        let microns = BrewFormat.Dial.microns
        XCTAssertEqual(ShotsGrind.parse("246", dial: microns), 246)
        XCTAssertNil(ShotsGrind.parse("", dial: microns))
        XCTAssertNil(ShotsGrind.parse("abc", dial: microns))
        XCTAssertNil(ShotsGrind.parse("2001", dial: microns))
        // A dial read in halves: "3.5" is 7 stored steps.
        let clicks = BrewFormat.Dial(unit: "clicks", divisor: 2, subSteps: 0)
        XCTAssertEqual(ShotsGrind.parse("3.5", dial: clicks), 7)
        XCTAssertEqual(ShotsGrind.parse("3,5", dial: clicks), 7)
        XCTAssertEqual(clicks.setting(7), "3.5")
        // Two rings, 5 inner steps per outer step: "12:3" is step (12-1)*5+3.
        let rings = BrewFormat.Dial(unit: "", divisor: 1, subSteps: 5)
        XCTAssertEqual(ShotsGrind.parse("12:3", dial: rings), 58)
        XCTAssertEqual(rings.setting(58), "12:3")
        XCTAssertEqual(ShotsGrind.parse("12", dial: rings), 56)
        XCTAssertNil(ShotsGrind.parse("12:6", dial: rings))
    }

    func testShotsPlotPoints() throws {
        let data = try BrewAPI.decode(ShotDataModel.self, from: fixture("shot_data"))
        let all = ShotsPlot.points(data.timeS, data.weightG, limit: 100_000)
        XCTAssertEqual(all.count, min(data.timeS.count, data.weightG.count))
        let few = ShotsPlot.points(data.timeS, data.weightG, limit: 10)
        XCTAssertLessThanOrEqual(few.count, 12)
        XCTAssertEqual(few.last?.t, data.timeS.last)
        XCTAssertTrue(ShotsPlot.points([], [], limit: 10).isEmpty)
        XCTAssertEqual(ShotsPlot.quarters(of: 40), [0, 10, 20, 30, 40])
    }
}
