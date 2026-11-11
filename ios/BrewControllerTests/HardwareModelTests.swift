// The Grinder, Machine, Devices, Setup and Logs models decoded from JSON captured from the real routes
// (Fixtures/<screen>_*.json, from `bash test/hostapp/run.sh --dump DIR`), plus the pure logic those screens use.
import XCTest
@testable import BrewController

private func fixture(_ name: String, file: StaticString = #filePath) throws -> Data {
    let url = URL(fileURLWithPath: "\(file)").deletingLastPathComponent()
        .appendingPathComponent("Fixtures").appendingPathComponent(name + ".json")
    return try Data(contentsOf: url)
}

private func decode<T: Decodable>(_ type: T.Type, _ json: String) throws -> T {
    try BrewAPI.decode(type, from: Data(json.utf8))
}

final class HardwareModelTests: XCTestCase {
    // MARK: Grinder

    func testGrinderAdviceSettingsFromTheControllersSettings() throws {
        let s = try BrewAPI.decode(GrinderAdviceSettings.self, from: fixture("settings_idle"))
        XCTAssertEqual(s.grindToleranceS, 2)
        XCTAssertEqual(s.grindWindow, 3)
        XCTAssertTrue(s.grindRatedTarget)
        XCTAssertTrue(s.grindConfidence)
        XCTAssertEqual(s.grindRoom, 1, "grindRoom true is the strict rule")
        XCTAssertEqual(s.grindUmPerS, 3)
        XCTAssertEqual(s.grindMaxStepUm, 20)
        XCTAssertEqual(s.grindTimeBasis, 0)
        XCTAssertEqual(try decode(GrinderAdviceSettings.self, #"{"grindRoom":2}"#).grindRoom, 2)
        XCTAssertEqual(try decode(GrinderAdviceSettings.self, #"{"grindRoom":false}"#).grindRoom, 0)
    }

    func testGrinderSharedList() throws {
        XCTAssertEqual(try BrewAPI.decode(GrinderSharedList.self, from: fixture("grinder_community")).grinders.count, 0)
        let list = try decode(GrinderSharedList.self, #"{"v":1,"grinders":[{"id":"x-1","model":"Hand Mill","unit":"click","um_per_unit":30,"sub_steps":9,"min":1,"max":90,"burrs":"48 mm"},{"id":"x-2"}]}"#)
        XCTAssertEqual(list.grinders.count, 2)
        XCTAssertEqual(list.grinders[0].subSteps, 9)
        XCTAssertEqual(list.grinders[0].umPerUnit, 30)
        XCTAssertEqual(list.grinders[1].model, "")
        XCTAssertEqual(list.grinders[1].divisor, 1)
    }

    func testGrinderMeasuredIsOptional() throws {
        XCTAssertNil(try BrewAPI.decode(GrinderMeasured.self, from: fixture("grind_idle")).gapUm)
        let m = try decode(GrinderMeasured.self, #"{"grinder_gap_um":251.5,"grinder_dose_g":18.04,"grinder_age_s":600}"#)
        XCTAssertEqual(m.gapUm, 251.5)
        XCTAssertEqual(m.ageS, 600)
    }

    func testSettingParser() {
        let microns = BrewFormat.Dial.microns
        XCTAssertEqual(GrinderSettingParser.parse("246", dial: microns), 246)
        XCTAssertNil(GrinderSettingParser.parse("0", dial: microns))
        XCTAssertNil(GrinderSettingParser.parse("2001", dial: microns))
        XCTAssertNil(GrinderSettingParser.parse("abc", dial: microns))
        let halves = BrewFormat.Dial(unit: "click", divisor: 2)
        XCTAssertEqual(GrinderSettingParser.parse("3.5", dial: halves), 7)
        XCTAssertEqual(GrinderSettingParser.parse("3,5", dial: halves), 7)
        let ringed = BrewFormat.Dial(unit: "setting", divisor: 1, subSteps: 9)
        XCTAssertEqual(GrinderSettingParser.parse("12:3", dial: ringed), 11 * 9 + 3)
        XCTAssertNil(GrinderSettingParser.parse("12:10", dial: ringed))
        XCTAssertEqual(ringed.setting(GrinderSettingParser.parse("12:3", dial: ringed) ?? 0), "12:3")
    }

    func testAdviceTextFollowsTheAction() throws {
        let idle = try BrewAPI.decode(GrindAdviceModel.self, from: fixture("grind_idle"))
        XCTAssertEqual(GrinderAdviceText.make(idle, dial: .microns).title, "Enter the grinder’s setting")
        var advice = GrindAdviceModel()
        advice.action = "finer"
        advice.currentUm = 250
        advice.suggestedUm = 240
        advice.deltaUm = 10
        advice.errorS = -4
        advice.shotsUsed = 3
        advice.targetTimeS = 28
        advice.toleranceS = 2
        let finer = GrinderAdviceText.make(advice, dial: .microns)
        XCTAssertEqual(finer.title, "Go finer 10 um, to 240 um")
        XCTAssertEqual(finer.applyUm, 240)
        XCTAssertEqual(finer.applyLabel, "I set the grinder to 240")
        advice.action = "hold"
        let hold = GrinderAdviceText.make(advice, dial: .microns)
        XCTAssertEqual(hold.title, "Keep 250 um")
        XCTAssertEqual(hold.tone, .ok)
        XCTAssertNil(hold.applyUm)
    }

    func testChartPointsKeepOnlyCountedShots() {
        var a = ShotModel(); a.id = 1; a.endReason = "weight"; a.grindUm = 250; a.shotTimeS = 27; a.rating = 5
        var b = ShotModel(); b.id = 2; b.endReason = "user"; b.grindUm = 250; b.shotTimeS = 20
        var c = ShotModel(); c.id = 3; c.endReason = "weight"; c.grindUm = 240; c.shotTimeS = 30; c.grindVote = "ignore"
        var d = ShotModel(); d.id = 4; d.endReason = "weight"; d.grindUm = 245; d.shotTimeS = 29; d.pourTimeS = 25; d.grinderCalibration = 1
        var e = ShotModel(); e.id = 5; e.endReason = "weight"; e.grindUm = 255; e.shotTimeS = 26
        // Newest first, as /api/shots answers.
        let points = GrinderChartData.points([e, d, c, b, a], calibration: 0, pourBasis: false)
        XCTAssertEqual(points.map(\.id), [1, 5])
        XCTAssertEqual(points.map(\.order), [0, 1])
        XCTAssertEqual(GrinderChartData.points([e, d, c, b, a], calibration: 1, pourBasis: true).map(\.time), [25])
    }

    func testGrinderFormBodyForAListedModelAndATypedOne() {
        let model = GrinderCatalogEntry(id: 5, model: "Niche Zero", burrs: "63 mm conical", unit: "dial", umPerUnit: 0,
                                        divisor: 1, subSteps: 0, min: 1, max: 50, libraryId: "niche-zero")
        var form = GrinderFormDraft()
        form.pick = "own:5"
        form.fill(model: model.model, unit: model.unit, divisor: model.divisor, subSteps: model.subSteps, min: 1, max: 50)
        XCTAssertEqual(form.minText, "1")
        XCTAssertEqual(form.maxText, "50")
        XCTAssertEqual(form.kind, .dial)
        let body = form.body(models: [model], shared: [])
        XCTAssertEqual(body["modelId"], .int(5))
        XCTAssertEqual(body["min"], .int(1))
        XCTAssertEqual(body["max"], .int(50))
        XCTAssertEqual(body["unit"], .string("dial"))
        // Typed: no listed model, microns per step stays 0 (learned from the shots).
        var typed = GrinderFormDraft()
        typed.name = "My hand grinder"
        typed.kind = .steps
        typed.maxText = "60"
        let own = typed.body(models: [model], shared: [])
        XCTAssertEqual(own["modelId"], .int(0))
        XCTAssertEqual(own["model"], .string("My hand grinder"))
        XCTAssertEqual(own["unit"], .string("step"))
        XCTAssertEqual(own["umPerUnit"], .double(0))
        XCTAssertEqual(own["max"], .int(60))
        XCTAssertEqual(typed.body(models: [], shared: [], asOther: true)["asOther"], .bool(true))
    }

    func testGrinderFormMicroRing() {
        var form = GrinderFormDraft()
        form.name = "Sette"
        form.microOn = true
        form.subStepsText = "9"
        form.minText = "1"
        form.maxText = "31"
        XCTAssertEqual(form.range.0, 1)
        XCTAssertEqual(form.range.1, 279)
        XCTAssertEqual(form.body(models: [], shared: [])["subSteps"], .int(9))
        form.microOn = false
        form.maxText = "0"
        XCTAssertTrue(form.rangeUnreadable)
    }

    // MARK: Machine

    func testMachineFixtures() throws {
        XCTAssertEqual(try BrewAPI.decode(MachineSharedList.self, from: fixture("machine_community")).machines.count, 0)
        XCTAssertEqual(try BrewAPI.decode([MachinePowerEvent].self, from: fixture("machine_events")).count, 0)
        XCTAssertEqual(try BrewAPI.decode(MachineReviewNote.self, from: fixture("machine_idle")).note, "")
        let events = try decode([MachinePowerEvent].self, #"[{"t":1790000000,"on":true,"by":"schedule","ok":true,"sure":true,"outcome":"pressed"}]"#)
        XCTAssertEqual(events[0].by, "schedule")
        XCTAssertTrue(events[0].on)
        let list = try decode(MachineSharedList.self, #"{"v":1,"machines":[{"id":"m-1","maker":"Gaggia","model":"Classic","control":"rocker","style":"toggle_button","self_stop_s":0}]}"#)
        XCTAssertEqual(list.machines[0].profile.libraryId, "m-1")
        XCTAssertEqual(list.machines[0].profile.builtin, 0)
        let review = try decode(MachineReviewNote.self, #"{"current":{"review_note":"Needs a control"}}"#)
        XCTAssertEqual(review.note, "Needs a control")
    }

    func testMachineStylesAreTheFiveOfTheProfile() {
        XCTAssertEqual(MachineStyle.allCases.map(\.rawValue),
                       ["toggle_button", "start_button", "start_stop_buttons", "hold_button", "manual"])
    }

    func testMachineDraftSendsABuiltinByNumberUntilItIsChanged() throws {
        let current = try BrewAPI.decode(MachineModel.self, from: fixture("machine_idle")).current
        XCTAssertEqual(current.builtin, 1)
        var draft = MachineDraft(profile: current)
        XCTAssertEqual(draft.body["builtin"], .int(1))
        XCTAssertEqual(draft.body["auto_off_min"], .int(240))
        draft.selfStopText = "30"
        let changed = draft.body
        XCTAssertNil(changed["builtin"])
        XCTAssertEqual(changed["self_stop_s"], .int(30))
        XCTAssertEqual(changed["style"], .string("toggle_button"))
    }

    // MARK: Devices

    func testDevicesDetailAndPusher() throws {
        let detail = try BrewAPI.decode(DevicesDetail.self, from: fixture("devices_idle"))
        XCTAssertEqual(detail.botProblem, "")
        XCTAssertNil(detail.powerBattery)
        XCTAssertEqual(try decode(DevicesDetail.self, #"{"power_battery":61,"power_problem":"asleep"}"#).powerBattery, 61)
        let brew = try BrewAPI.decode(DevicesPusher.self, from: fixture("devices_pusher_brew"))
        XCTAssertEqual(brew.role, "brew")
        XCTAssertFalse(brew.tuyaLocalKeySet)
        let power = try BrewAPI.decode(DevicesPusher.self, from: fixture("devices_pusher_power"))
        XCTAssertEqual(power.role, "power")
        XCTAssertFalse(power.passwordSet)
        XCTAssertEqual(try BrewAPI.decode(DevicesBotTest.self, from: fixture("devices_bot_test")).brew, "none")
        // The seen list is Core's model: both devices, with their roles.
        let devices = try BrewAPI.decode(DevicesModel.self, from: fixture("devices_idle"))
        XCTAssertEqual(devices.seen.map(\.kind), ["scale", "bot"])
    }

    // MARK: Setup

    func testSetupFixtures() throws {
        let start = try BrewAPI.decode(SetupStart.self, from: fixture("setup_start"))
        XCTAssertTrue(start.scalePaired && start.botPaired)
        XCTAssertFalse(start.powerBotPaired)
        XCTAssertEqual(start.machineMaker, "Breville")
        XCTAssertEqual(start.recipes, 2)
        XCTAssertFalse(start.grinderSet)
        let ha = try BrewAPI.decode(SetupHomeAssistant.self, from: fixture("setup_homeassistant"))
        XCTAssertEqual(ha.url, "")
        XCTAssertFalse(ha.tokenSet)
        let share = try BrewAPI.decode(SetupShareStatus.self, from: fixture("setup_share"))
        XCTAssertTrue(share.keyReady)
        XCTAssertTrue(share.viaRelay)
        XCTAssertFalse(share.realSignatures)
        XCTAssertEqual(share.algorithm, "HOST-UNSIGNED")
        let report = try BrewAPI.decode(SetupDiagReport.self, from: fixture("setup_diag_report"))
        XCTAssertTrue(report.on)
        XCTAssertTrue(report.relayDefault)
        XCTAssertEqual(SetupDiagnosticsView.crashSummary(try fixture("setup_crash")), "")
        XCTAssertEqual(SetupDiagnosticsView.crashSummary(Data(#"{"reason":"panic","task":"loop"}"#.utf8)), "reason panic, task loop")
        let drinkers = try decode(SetupDrinkers.self, #"{"named":true,"drinkers":["Ann","Bo","","","","","",""],"attrs":[]}"#)
        XCTAssertTrue(drinkers.named)
        XCTAssertEqual(drinkers.drinkers.count, 8)
    }

    func testSetupRestoreSummaryAndAmbientTest() throws {
        let summary = try decode(SetupRestoreSummary.self, #"{"ok":true,"created":1790000000,"firmware":"0.9.2","key_id":"abc","history":true,"secrets":false,"records":41,"files":7,"bytes":20480}"#)
        XCTAssertTrue(summary.history)
        XCTAssertFalse(summary.secrets)
        XCTAssertEqual(summary.records, 41)
        let ambient = try decode(SetupAmbientTest.self, #"{"ok":true,"problem":"","reading":{"rh_pct":48,"temp_c":21.5}}"#)
        XCTAssertEqual(ambient.rhPct, 48)
        XCTAssertEqual(ambient.tempC, 21.5)
        XCTAssertFalse(try decode(SetupAmbientTest.self, #"{"ok":false,"problem":"no entity"}"#).ok)
    }

    @MainActor
    func testShowInMorePanelBits() {
        let store = SetupStore()
        let logs = SetupStore.panels.first { $0.id == "logs" }!
        let start = SetupStore.panels.first { $0.id == "start" }!
        XCTAssertFalse(store.panelShown(logs))
        XCTAssertTrue(store.panelShown(start), "the guide's bit means hidden")
        store.settings.webPanels = 16 | 64
        XCTAssertTrue(store.panelShown(logs))
        XCTAssertFalse(store.panelShown(start))
    }

    // MARK: Logs

    func testLogsConfig() throws {
        let config = try BrewAPI.decode(LogsConfig.self, from: fixture("logs_config"))
        XCTAssertEqual(config.level, 2)
        XCTAssertTrue(config.keep)
        XCTAssertEqual(config.flashLimit, 524_288)
        XCTAssertEqual(config.memoryBytes, 49_152)
        XCTAssertEqual(LogsLevel.names.count, 4)
    }

    // MARK: Gaps found in the build-3 audit

    func testGrinderMakerGroupsLikeTheWebPage() {
        XCTAssertEqual(GrinderMaker.of("Barista Express (built in)"), "Breville")
        XCTAssertEqual(GrinderMaker.of("DF64 Gen 2"), "Turin (DF)")
        XCTAssertEqual(GrinderMaker.of("Mahlkonig EK43"), "Mahlkönig")
        XCTAssertEqual(GrinderMaker.of("Rancilio Silvia"), "Rancilio")
        XCTAssertEqual(GrinderMaker.of(""), "Other")
    }

    func testPickingAModelFillsTheFormButLoadingDoesNot() {
        var g = GrinderModel()
        g.model = "My Grinder"
        g.unit = "click"
        g.min = 5
        g.max = 40
        let loaded = GrinderFormDraft(grinder: g)
        XCTAssertEqual(loaded.minText, "5")
        XCTAssertEqual(loaded.maxText, "40")
        XCTAssertEqual(loaded.unit, "click", "the saved unit stays: only picking a model fills from the catalogue")
    }

    func testAddonListDecodesFieldsMapAndLastGrind() throws {
        let json = """
        {"compiled":1,"addons":[{"id":"x","name":"X","version":"1.0","kind":"grinder","api_ok":true,"enabled":true,
        "link":"connected","status":"ok","shot_feedback_possible":true,
        "fields":[{"key":"host","label":"Host","type":"text","help":"","value":"a"},
                  {"key":"on","label":"On","type":"bool","value":"1"},
                  {"key":"pw","label":"Password","type":"secret","set":true}],
        "options":{"dose_sets_target":true},"map":[{"grinder_recipe":2,"recipe_id":7,"source":"auto"}],
        "last_grind":{"age_s":120,"grinder_recipe":2,"dose_g":18.1,"mapped":true,"recipe_id":7,"setting":210}}]}
        """
        let list = try BrewAPI.decode(SetupAddonList.self, from: Data(json.utf8))
        XCTAssertEqual(list.addons.count, 1)
        let a = list.addons[0]
        XCTAssertEqual(a.fields.map(\.type), ["text", "bool", "secret"])
        XCTAssertEqual(a.fields[1].value, "1")
        XCTAssertTrue(a.fields[2].set)
        XCTAssertTrue(a.options.doseSetsTarget)
        XCTAssertEqual(a.map.first?.recipeId, 7)
        XCTAssertEqual(a.lastGrind?.setting, "210")
        XCTAssertEqual(try BrewAPI.decode(SetupAddonList.self, from: Data("{}".utf8)).addons.count, 0)
    }

    func testTimeZoneRuleMatchesThePosixFormOfTheWebPage() {
        let berlin = MachineTimeZone.posix(zone: TimeZone(identifier: "Europe/Berlin")!, year: 2026)
        XCTAssertEqual(berlin, "<+01>-1<+02>,M3.5.0,M10.5.0/3")
        let newYork = MachineTimeZone.posix(zone: TimeZone(identifier: "America/New_York")!, year: 2026)
        XCTAssertEqual(newYork, "<-05>5<-04>,M3.2.0,M11.1.0")
        let tokyo = MachineTimeZone.posix(zone: TimeZone(identifier: "Asia/Tokyo")!, year: 2026)
        XCTAssertEqual(tokyo, "<+09>-9")
        let india = MachineTimeZone.posix(zone: TimeZone(identifier: "Asia/Kolkata")!, year: 2026)
        XCTAssertEqual(india, "<+0530>-5:30")
    }
}
