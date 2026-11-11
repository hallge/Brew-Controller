// The models and sums of the Insights, Subscriptions, Discover and Community screens, decoded from JSON
// captured from the real routes: Fixtures/insights_*, subscriptions_*, discover_*, community_*.json
// (written by `test/hostapp/run.sh --dump DIR`; the *_published/_listed/_review/_found/_taste ones are
// written by hand from what the web page reads, since the library relay is not reachable in the runner).
import XCTest
@testable import BrewController

/// Loads Fixtures/<name>.json next to this file.
private func screenFixture(_ name: String, file: StaticString = #filePath) throws -> Data {
    let url = URL(fileURLWithPath: "\(file)").deletingLastPathComponent()
        .appendingPathComponent("Fixtures").appendingPathComponent(name + ".json")
    return try Data(contentsOf: url)
}

private func decode<T: Decodable>(_ type: T.Type, _ name: String) throws -> T {
    try BrewAPI.decode(type, from: screenFixture(name))
}

final class DiscoverScreensModelTests: XCTestCase {
    // MARK: Insights

    func testTasteProfile() throws {
        let profile = try decode(InsightsTasteProfile.self, "insights_taste_profile")
        XCTAssertEqual(profile.dims.count, 13)
        XCTAssertEqual(profile.cupLabels, ["Acidity", "Sweetness", "Body", "Bitterness", "Finish"])
        XCTAssertEqual(profile.flavourLabels.count, 8)
        XCTAssertEqual(profile.flavourLabels.first, "Fruity")
        XCTAssertEqual(profile.cupValues, [0.75, 0.5, 0.5, 0.25, 0.75])
        XCTAssertEqual(profile.flavourValues.count, 8)
        XCTAssertTrue(profile.profile.shown)
        XCTAssertFalse(profile.profile.settled)
        XCTAssertEqual(profile.profile.coffees, 3)
        XCTAssertEqual(profile.best.map { $0.coffee }, [2, 3])
        let note = InsightsMath.tasteNote(profile)
        XCTAssertTrue(note.contains("You lean towards"), note)
        XCTAssertTrue(note.contains("away from"), note)
        XCTAssertTrue(note.contains("settles from 8"), note)
        let none = InsightsTasteProfile()
        XCTAssertTrue(InsightsMath.tasteNote(none).hasPrefix("No coffees rated yet."))
    }

    func testRatingsDrinkers() throws {
        let ratings = try decode(InsightsRatings.self, "insights_ratings")
        XCTAssertFalse(ratings.named)
        XCTAssertEqual(ratings.drinkers.count, 8)
        XCTAssertTrue(ratings.people.isEmpty)
        let named = try BrewAPI.decode(InsightsRatings.self, from: Data(#"{"named":true,"drinkers":["Ann","","Bo"]}"#.utf8))
        XCTAssertEqual(named.people.map { $0.name }, ["Ann", "Bo"])
        XCTAssertEqual(named.people.map { $0.id }, [1, 3])
    }

    func testGrindAccuracyAndPowerEventsOffline() throws {
        let accuracy = try decode(InsightsGrindAccuracy.self, "insights_grind_accuracy")
        XCTAssertEqual(accuracy.toleranceS, 2)
        XCTAssertEqual(accuracy.score, 0.3, accuracy: 0.001)
        XCTAssertTrue(accuracy.events.isEmpty)
        XCTAssertTrue(InsightsMath.accuracyPoints(events: accuracy.events, tolerance: 2).isEmpty)
        let events = try decode([InsightsPowerEvent].self, "insights_power_events")
        XCTAssertTrue(events.isEmpty)
        let one = try BrewAPI.decode([InsightsPowerEvent].self,
                                     from: Data(#"[{"t":1794432911,"on":true,"by":"schedule","ok":true,"sure":true,"outcome":""}]"#.utf8))
        XCTAssertEqual(one.first?.by, "schedule")
        XCTAssertEqual(one.first?.on, true)
    }

    func testShotsFeedTheCharts() throws {
        let shots = try decode([ShotModel].self, "insights_shots")
        XCTAssertEqual(shots.count, 1)
        XCTAssertTrue(InsightsMath.byWeight(shots[0]))
        XCTAssertNotNil(shots[0].date)
        // The demo shot has a target time and a date, so the calendar and dial-in charts have data; it has
        // no coffee, grind setting or roast date, so the roast and age charts say what they need.
        let bags = InsightsMath.dialInBags(shots: shots, tolerance: 2)
        XCTAssertEqual(bags.count, 1)
        XCTAssertEqual(bags[0].total, 1)
        XCTAssertTrue(InsightsMath.roastGrindPoints(shots: shots, calibration: 0, dial: .microns).isEmpty)
        XCTAssertNil(InsightsMath.ageHeat(shots: shots))
    }

    private func shot(id: Int, time: String, error: Double, target: Double = 28, coffeeId: Int = 1, coffee: String = "Guji",
                      recipe: String = "Double", roast: Int = 0, grind: Int = 0, level: String = "", flow: Double = 1.5,
                      calibration: Int = 0) -> ShotModel {
        var shot = ShotModel()
        shot.id = id
        shot.timestamp = time
        shot.endReason = "weight"
        shot.targetTimeS = target
        shot.shotTimeS = target + error
        shot.coffeeId = coffeeId
        shot.coffee = coffee
        shot.recipe = recipe
        shot.roastDate = roast
        shot.grindUm = grind
        shot.roastLevel = level
        shot.pourFlowGS = flow
        shot.grinderCalibration = calibration
        shot.finalWeightG = 36
        return shot
    }

    func testDialInBags() {
        let shots = [
            shot(id: 1, time: "2026-11-01T10:00:00Z", error: -5),
            shot(id: 2, time: "2026-11-02T10:00:00Z", error: 0.5),
            shot(id: 3, time: "2026-11-03T10:00:00Z", error: 4),
            shot(id: 4, time: "2026-11-04T10:00:00Z", error: 1, coffeeId: 2, coffee: "Huila"),
        ]
        let bags = InsightsMath.dialInBags(shots: shots, tolerance: 2)
        XCTAssertEqual(bags.count, 2)
        XCTAssertEqual(bags[0].dial, 2)
        XCTAssertEqual(bags[0].slowAfter, 1)
        XCTAssertEqual(bags[0].onTimeAfter, 0)
        XCTAssertEqual(bags[0].lastPace, .slow)
        XCTAssertEqual(bags[1].dial, 1)
        XCTAssertEqual(InsightsMath.dialInNote(bags), "2 bags, 1.5 shots to dial in on average, 1 drifted off since")
    }

    func testRoastGrindAndBands() {
        let shots = [
            shot(id: 1, time: "2026-11-01T10:00:00Z", error: 0, grind: 240, level: "light"),
            shot(id: 2, time: "2026-11-02T10:00:00Z", error: 1, grind: 250, level: "light"),
            shot(id: 3, time: "2026-11-03T10:00:00Z", error: 6, grind: 260, level: "light"),
            shot(id: 4, time: "2026-11-04T10:00:00Z", error: 0, grind: 300, level: "dark", calibration: 7),
            shot(id: 5, time: "2026-11-05T10:00:00Z", error: 0, grind: 0, level: "light"),
        ]
        let points = InsightsMath.roastGrindPoints(shots: shots, calibration: 0, dial: .microns)
        XCTAssertEqual(points.map { $0.id }, [1, 2, 3])
        let bands = InsightsMath.onTimeBands(points: points, tolerance: 2)
        XCTAssertEqual(bands.count, 1)
        XCTAssertEqual(bands[0].low, 240)
        XCTAssertEqual(bands[0].high, 250)
        XCTAssertEqual(bands[0].count, 2)
        XCTAssertEqual(InsightsMath.axisValue(7, dial: BrewFormat.Dial(unit: "clicks", divisor: 2, subSteps: 0)), 3.5)
    }

    func testAgeHeatAndAge() throws {
        let shots = [
            shot(id: 1, time: "2026-11-11T12:00:00Z", error: 0, roast: 20261101),
            shot(id: 2, time: "2026-11-20T12:00:00Z", error: 3, roast: 20261101),
            shot(id: 3, time: "2026-11-20T12:00:00Z", error: 0, roast: 0),
        ]
        let age = try XCTUnwrap(InsightsMath.ageDays(shots[0]))
        XCTAssertTrue((10...11).contains(age), "age \(age)")
        XCTAssertNil(InsightsMath.ageDays(shots[2]))
        let heat = try XCTUnwrap(InsightsMath.ageHeat(shots: shots))
        XCTAssertEqual(heat.total, 2)
        XCTAssertEqual(heat.leftOut, 1)
        XCTAssertEqual(heat.weeks, 3)
        XCTAssertEqual(heat.rows, 8)
        XCTAssertEqual(heat.counts.flatMap { $0 }.reduce(0, +), 2)
        XCTAssertEqual(InsightsMath.rowLabel(0), "≤ −6 s")
        XCTAssertEqual(InsightsMath.rowLabel(3), "-2 to 0 s")
        XCTAssertEqual(InsightsMath.rowLabel(7), "≥ +6 s")
    }

    func testCalendarDays() {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(identifier: "UTC") ?? .current
        let now = ISO8601DateFormatter().date(from: "2026-11-11T12:00:00Z") ?? Date()  // a Wednesday
        let shots = [shot(id: 1, time: "2026-11-11T08:00:00Z", error: 0), shot(id: 2, time: "2026-11-09T08:00:00Z", error: 4)]
        let days = InsightsMath.calendarDays(shots: shots, now: now, calendar: calendar)
        XCTAssertEqual(days.count, 25 * 7 + 3)
        XCTAssertEqual(days.last?.week, 25)
        XCTAssertEqual(days.last?.weekday, 2)
        XCTAssertEqual(days.last?.count, 1)
        XCTAssertEqual(days.first?.weekday, 0)
        XCTAssertEqual(days[days.count - 3].count, 1)
        XCTAssertEqual(days[days.count - 3].drift, 4)
        let points = InsightsMath.dayTimePoints(shots: shots, days: 14, now: now, calendar: calendar)
        XCTAssertEqual(points.count, 2)
        XCTAssertEqual(points.first { $0.id == 1 }?.row, 0)
        XCTAssertEqual(points.first { $0.id == 1 }?.hour ?? 0, 8, accuracy: 0.01)
        XCTAssertEqual(InsightsMath.hourRange(shotHours: [], powerHours: []), 6...22)
        XCTAssertEqual(InsightsMath.hourRange(shotHours: [8.2, 9.5], powerHours: []), 6...12)
    }

    func testRecipeTagsAndAccuracy() {
        let shots = [shot(id: 1, time: "", error: 0, recipe: "Double"), shot(id: 2, time: "", error: 0, recipe: "Double"),
                     shot(id: 3, time: "", error: 0, recipe: "Single")]
        let tags = InsightsMath.recipeTags(for: shots)
        XCTAssertEqual(tags["Double"], "D")
        XCTAssertEqual(tags["Single"], "S")
        var event = InsightsGrindEvent()
        event.targetS = 28
        event.timeS = 29
        event.followed = true
        let points = InsightsMath.accuracyPoints(events: [event, event, event], tolerance: 2)
        XCTAssertEqual(points.count, 3)
        XCTAssertEqual(points[0].confidence, 0.3, accuracy: 0.001)
        XCTAssertGreaterThan(points[2].confidence, 0.3)
        let summary = InsightsMath.onTargetPercent(points: points, tolerance: 2)
        XCTAssertEqual(summary?.percent, 100)
        XCTAssertEqual(summary?.followed, 3)
        XCTAssertEqual(InsightsMath.tolerance(nil), 2)
    }

    // MARK: Subscriptions

    func testSubscriptionList() throws {
        let list = try decode(SubscriptionList.self, "subscriptions_list")
        XCTAssertEqual(list.subscriptions.count, 2)
        XCTAssertEqual(list.max, 12)
        XCTAssertEqual(list.monthlyCents, 2413)
        XCTAssertEqual(list.today, 20261111)
        XCTAssertFalse(list.sharing)
        let coffee = list.subscriptions[0]
        XCTAssertEqual(coffee.name, "Demo Roasters Monthly")
        XCTAssertEqual(coffee.kind, "coffee")
        XCTAssertEqual(coffee.everyDays, 28)
        XCTAssertEqual(coffee.daysUntil, 9)
        XCTAssertFalse(coffee.dueSoon)
        XCTAssertEqual(coffee.share, "local")
        XCTAssertTrue(coffee.canShare)
        let supplies = list.subscriptions[1]
        XCTAssertEqual(supplies.kind, "supplies")
        XCTAssertEqual(supplies.item, "paper filters")
        XCTAssertEqual(supplies.stock, 40)
        XCTAssertEqual(supplies.perShot, 1)
        XCTAssertEqual(supplies.stockDaysLeft, 40)
        XCTAssertNil(coffee.stockDaysLeft)
        XCTAssertEqual(list.subscription(id: 2)?.name, "Paper filters")
        // What is posted back keeps the fields the controller reads.
        let body = supplies.json
        XCTAssertEqual(body["every_days"], .int(60))
        XCTAssertEqual(body["per_delivery"], .int(100))
        XCTAssertEqual(body["item"], .string("paper filters"))
        XCTAssertEqual(body["manage_url"], .string(""))
    }

    func testSubscriptionWording() {
        XCTAssertEqual(SubscriptionWording.every(days: 28), "every 4 weeks")
        XCTAssertEqual(SubscriptionWording.every(days: 7), "every week")
        XCTAssertEqual(SubscriptionWording.every(days: 30), "every month")
        XCTAssertEqual(SubscriptionWording.every(days: 10), "every 10 days")
        XCTAssertEqual(SubscriptionWording.every(days: 0), "not regular")
        XCTAssertEqual(SubscriptionWording.money(cents: 1800), "$18.00")
        XCTAssertEqual(SubscriptionWording.cadenceText([14, 28]), "every 2 wk / 4 wk")
        XCTAssertEqual(SubscriptionWording.priceFrom(cents: 1800, currency: "USD"), "from $18.00")
        XCTAssertEqual(SubscriptionWording.priceFrom(cents: 1500, currency: "EUR"), "from 15.00 EUR")
        XCTAssertEqual(SubscriptionWording.ymd(BrewFormat.date(fromYMD: 20261120)), 20261120)
        XCTAssertEqual(SubscriptionWording.perDay(1), "1 shot a day")
        XCTAssertEqual(SubscriptionWording.perDay(2.5), "2.5 shots a day")
        var due = SubscriptionItem()
        due.daysUntil = -2
        due.nextDate = 20261109
        XCTAssertTrue(SubscriptionWording.when(due).hasSuffix("2 days overdue"))
        due.daysUntil = 1
        XCTAssertEqual(SubscriptionWording.when(due), "tomorrow")
    }

    func testSubscriptionReviewsPreviewAndLibrary() throws {
        let reviews = try decode(SubscriptionReviewList.self, "subscriptions_reviews")
        XCTAssertEqual(reviews.reviews.count, 1)
        XCTAssertEqual(reviews.reviews[0].stars, 4)
        XCTAssertEqual(reviews.reviews[0].aspects["quality"], 5)
        XCTAssertEqual(reviews.reviews[0].aspects["value"], 3)
        XCTAssertNil(reviews.reviews[0].aspects["transparency"])
        let preview = try decode(SubscriptionPreview.self, "subscriptions_preview")
        XCTAssertEqual(preview.state, "none")
        let offline = try decode(SubscriptionLibraryList.self, "subscriptions_library")
        XCTAssertTrue(offline.services.isEmpty)
        let none = try decode(SubscriptionRecommendations.self, "subscriptions_recommend")
        XCTAssertEqual(none.basis, "none")
        XCTAssertTrue(none.items.isEmpty)
        XCTAssertTrue(none.shown)

        let listed = try decode(SubscriptionLibraryList.self, "subscriptions_library_listed")
        let service = try XCTUnwrap(listed.services.first)
        XCTAssertEqual(service.name, "Demo Beans Club")
        XCTAssertEqual(service.cadences, [14, 28])
        XCTAssertEqual(service.shipsTo, ["US", "CA"])
        XCTAssertEqual(service.reviews?.n, 12)
        XCTAssertEqual(service.style, ["single_roaster", "single_origin"])
        let add = service.addJSON
        XCTAssertEqual(add["library_id"], .string("svc-demo"))
        XCTAssertEqual(add["every_days"], .int(14))
        XCTAssertEqual(add["ships_to"], .string("US,CA"))
        let recs = try decode(SubscriptionRecommendations.self, "subscriptions_recommend_taste")
        XCTAssertEqual(recs.basis, "taste")
        XCTAssertEqual(recs.items.first?.why.count, 2)
        XCTAssertEqual(recs.items.first?.service.name, "Demo Beans Club")
        XCTAssertEqual(recs.items.first?.people, "Drinkers like you rate it 4.5")
    }

    // MARK: Discover

    func testCoffeeRecommendations() throws {
        let recs = try decode(DiscoverRecommendations.self, "discover_recommend")
        XCTAssertTrue(recs.shown)
        XCTAssertEqual(recs.coffees, 3)
        XCTAssertEqual(recs.items.count, 1)
        let item = recs.items[0]
        XCTAssertEqual(item.name, "Guji Natural")
        XCTAssertEqual(item.from, "shelf")
        XCTAssertEqual(item.why, ["fruity", "floral"])
        XCTAssertEqual(item.like, 3)
        XCTAssertEqual(item.likeStars, 4)
        XCTAssertLessThan(item.sure, 0.4)
        XCTAssertTrue(recs.buyAgain.isEmpty)
        XCTAssertEqual(discoverMoreCoffees(1), "2 more coffees")
        XCTAssertEqual(discoverMoreCoffees(2), "1 more coffee")
    }

    func testLibraryOfflineAndPublished() throws {
        let pending = try decode(DiscoverPending.self, "discover_pending")
        XCTAssertFalse(pending.linked)
        XCTAssertFalse(pending.canReview)
        XCTAssertTrue(pending.index.items.isEmpty)
        XCTAssertEqual(pending.kid, "8bba0d152ed2b5f7e605")
        XCTAssertTrue(try decode(DiscoverCoffeeIndex.self, "discover_community_coffees").coffees.isEmpty)
        XCTAssertTrue(try decode(DiscoverRecipeBook.self, "discover_community_recipes").items.isEmpty)

        let coffees = try decode(DiscoverCoffeeIndex.self, "discover_community_coffees_published")
        XCTAssertEqual(coffees.coffees.count, 2)
        XCTAssertEqual(coffees.coffees[0].detail, "Demo Roasters, Ethiopia, Natural")
        let adopt = coffees.coffees[0].adoptJSON
        XCTAssertEqual(adopt["libraryId"], .string("demo-roasters-guji-natural"))
        XCTAssertEqual(adopt["roastLevel"], .int(1))
        XCTAssertEqual(adopt["altitudeM"], .int(2100))
        XCTAssertEqual(coffees.coffees[1].adoptJSON["roastLevel"], .int(3))

        let book = try decode(DiscoverRecipeBook.self, "discover_community_recipes_published")
        XCTAssertEqual(book.items.count, 2)
        XCTAssertEqual(book.items[0].recipe.line, "18 g in, 36 g out, 28 s, hold 3 s")
        XCTAssertEqual(book.items[1].recipe.line, "18.5 g in, 46.3 g out, 32 s, bloom 5 s")
        XCTAssertEqual(book.items[0].contributors, 3)
        let draft = DiscoverRecipeDraft(name: "Classic 1:2", doseG: 18, yieldG: 36, timeS: 28, holdS: 3)
        XCTAssertEqual(draft.json["preinfusion"], .int(1))
        XCTAssertEqual(draft.json["targetWeightG"], .double(36))
        XCTAssertEqual(draft.json["id"], .int(0))
        XCTAssertNil(draft.json["bloomPauseS"])
    }

    func testPendingReview() throws {
        let pending = try decode(DiscoverPending.self, "discover_pending_review")
        XCTAssertTrue(pending.linked)
        XCTAssertTrue(pending.canReview)
        XCTAssertEqual(pending.index.items.count, 3)
        XCTAssertEqual(pending.myVerdict(for: "c-1"), true)
        XCTAssertNil(pending.myVerdict(for: "c-2"))
        let recipe = pending.index.items[1]
        XCTAssertEqual(recipe.type, "recipe")
        XCTAssertEqual(recipe.from, pending.kid)
        XCTAssertEqual(recipe.declines, 1)
        XCTAssertEqual(recipe.recipe.line, "18 g in, 40 g out, 30 s, hold 4 s")
        XCTAssertEqual(pending.index.items[0].coffee.name, "Sidamo")
    }

    // MARK: Community

    func testFeedOfflineAndListed() throws {
        let offline = try decode(CommunityLinks.self, "community_links")
        XCTAssertTrue(offline.listed.isEmpty)
        XCTAssertEqual(offline.mine.count, 1)
        XCTAssertEqual(offline.mine[0].state, "queued")
        XCTAssertTrue(offline.canSend)
        XCTAssertEqual(CommunityText.states["queued"], "waiting to send")

        let listed = try decode(CommunityLinks.self, "community_links_listed")
        XCTAssertEqual(listed.listed.count, 2)
        XCTAssertEqual(listed.listed[0].topics, ["dialing_in", "brewing"])
        XCTAssertEqual(CommunityText.thumbnail(image: listed.listed[0].image, url: listed.listed[0].url)?.absoluteString,
                       "https://i.ytimg.com/vi/dQw4w9WgXcQ/hqdefault.jpg")
        XCTAssertEqual(CommunityText.thumbnail(image: listed.listed[1].image, url: listed.listed[1].url)?.absoluteString,
                       "https://example.com/a.jpg")
        XCTAssertNil(CommunityText.thumbnail(image: "", url: "https://example.com/x"))
        XCTAssertEqual(CommunityText.youtubeID("https://youtu.be/dQw4w9WgXcQ"), "dQw4w9WgXcQ")
        XCTAssertNil(CommunityText.youtubeID("https://example.com/watch?v=dQw4w9WgXcQ"))
        XCTAssertEqual(CommunityText.topicLabel("dialing_in"), "Dialling in")
        XCTAssertEqual(CommunityText.kindLabel("podcast"), "Podcast")
        var draft = CommunitySuggestionDraft()
        draft.url = "https://example.com/v"
        draft.topics = ["beans"]
        XCTAssertEqual(draft.json["topics"], .array([.string("beans")]))
        XCTAssertEqual(draft.json["kind"], .string("video"))
    }

    func testTrendingGrindersShare() throws {
        XCTAssertTrue(try decode(CommunityTrending.self, "community_trending").recipes.isEmpty)
        XCTAssertEqual(try decode(CommunityTrending.self, "community_trending").days, 0)
        let trending = try decode(CommunityTrending.self, "community_trending_listed")
        XCTAssertEqual(trending.days, 30)
        XCTAssertEqual(trending.recipes.count, 2)
        XCTAssertEqual(CommunityText.trendingLine(trending.recipes[0]), "1:2.0 in about 28 s, held pre-infusion")
        XCTAssertEqual(CommunityText.trendingCounts(trending.recipes[0]), "5 people, 7 dial-ins, 3 coffees")
        XCTAssertEqual(CommunityText.trendingCounts(trending.recipes[1]), "2 people, 2 dial-ins, 1 coffee")

        XCTAssertTrue(try decode(CommunityGrinderList.self, "community_grinders").grinders.isEmpty)
        let grinders = try decode(CommunityGrinderList.self, "community_grinders_listed")
        XCTAssertEqual(grinders.grinders.map { $0.model }, ["Niche Zero", "DF64"])

        let share = try decode(CommunityShareStatus.self, "community_share")
        XCTAssertTrue(share.keyReady)
        XCTAssertEqual(share.waiting, 2)
        XCTAssertEqual(share.sent, 0)
        XCTAssertEqual(share.problem, "the relay could not be reached")
        XCTAssertEqual(share.capacity, 300)
    }

    func testForWhatYouAreBrewing() throws {
        // Without a coffee id the controller answers {"ok":false,...}: nothing to show.
        let missing = try decode(CommunityNow.self, "community_now")
        XCTAssertFalse(missing.found)
        let found = try decode(CommunityNow.self, "community_now_found")
        XCTAssertTrue(found.found)
        XCTAssertEqual(found.contributors, 6)
        XCTAssertEqual(found.start.setting, 246)
        XCTAssertEqual(found.start.basis, "fitted")
        let why = CommunityText.startWhy(found)
        XCTAssertTrue(why.hasPrefix("translated to your grinder through 2 coffees you have in common with others"), why)
        XCTAssertTrue(why.contains("nobody has settled this exact recipe"), why)
    }

    func testRedditText() {
        var shot = ShotModel()
        shot.id = 4
        shot.timestamp = "2026-11-11T12:00:00Z"
        shot.doseG = 18
        shot.finalWeightG = 37.2
        shot.shotTimeS = 26.1
        shot.firstDropS = 8.4
        shot.pourFlowGS = 2.1
        shot.peakFlowGS = 2.05
        shot.grindUm = 246
        shot.taste = "balanced"
        shot.rating = 4
        var coffee = CoffeeModel()
        coffee.name = "Guji Natural"
        coffee.roaster = "Demo Roasters"
        coffee.origin = "Ethiopia"
        coffee.process = "Natural"
        coffee.notes = "jasmine"
        var grinder = GrinderModel()
        grinder.model = "Niche Zero"
        grinder.unit = "um"
        let item = CommunityText.RedditShot(shot: shot, coffee: coffee, grinder: grinder)
        XCTAssertEqual(CommunityText.redditTitle(item), "Guji Natural (Demo Roasters): 18.0 g in, 37.2 g out in 26.1 s")
        let text = CommunityText.redditText(item)
        XCTAssertTrue(text.contains("**Coffee:** Guji Natural from Demo Roasters (Ethiopia, Natural)"), text)
        XCTAssertTrue(text.contains("**Recipe:** 18.0 g in, 37.2 g out (1:2.1) in 26.1 s; first drops at 8.4 s"), text)
        XCTAssertTrue(text.contains("**Grind:** 246 um on a Niche Zero"), text)
        XCTAssertTrue(text.contains("**Flow:** 2.10 g/s on average while pouring, peak 2.05 g/s"), text)
        XCTAssertTrue(text.contains("**Taste:** balanced, 4/5"), text)
        XCTAssertTrue(text.contains("**Tasting notes on the bag:** jasmine"), text)
        XCTAssertTrue(text.hasSuffix("https://github.com/hallge/Brew-Controller*"), text)
        XCTAssertFalse(text.contains("\n\n\n"))
        let rows = CommunityText.redditRows(item)
        XCTAssertEqual(rows.first?.label, "Dose")
        XCTAssertEqual(rows.first { $0.label == "Start" }?.value, "machine pre-infusion")
        XCTAssertNil(rows.first { $0.label == "Roasted" })
        let bare = CommunityText.RedditShot(shot: ShotModel(), coffee: nil, grinder: GrinderModel())
        XCTAssertEqual(bare.name, "Espresso")
        XCTAssertTrue(CommunityText.redditText(bare).contains("**Grind:** not recorded"))
    }
}
