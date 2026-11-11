// The Codable models decoded from JSON captured from the real routes: Fixtures/*.json, written by
// `test/hostapp/run.sh --dump ios/BrewControllerTests/Fixtures` (the firmware running headless in demo
// mode, brewing one shot).
import XCTest
@testable import BrewController

/// Loads Fixtures/<name>.json next to this file (found by #filePath, so no bundle resources are needed).
private func fixture(_ name: String, file: StaticString = #filePath) throws -> Data {
    let url = URL(fileURLWithPath: "\(file)").deletingLastPathComponent()
        .appendingPathComponent("Fixtures").appendingPathComponent(name + ".json")
    return try Data(contentsOf: url)
}

final class CoreModelFixtureTests: XCTestCase {
    func testStatus() throws {
        let idle = try BrewAPI.decode(StatusModel.self, from: fixture("status_idle"))
        XCTAssertEqual(idle.state, "READY")
        XCTAssertEqual(idle.targetWeightG, 36)
        XCTAssertEqual(idle.machineStyle, "toggle_button")
        XCTAssertEqual(idle.scaleBattery, 74)
        XCTAssertEqual(idle.botBattery, 87)
        XCTAssertTrue(idle.scaleConnected && idle.botConnected && idle.cupPresent)
        XCTAssertTrue(idle.capabilities.contains("library_v1"))
        let brewing = try BrewAPI.decode(StatusModel.self, from: fixture("status_brewing"))
        XCTAssertEqual(brewing.state, "BREWING")
        let result = try BrewAPI.decode(StatusModel.self, from: fixture("status_result"))
        XCTAssertEqual(result.state, "SHOT_COMPLETE")
        XCTAssertEqual(result.lastShotId, 1)
    }

    func testScreenPanels() throws {
        let home = try BrewAPI.decode(ScreenSnapshot.self, from: fixture("screen_idle"))
        XCTAssertEqual(home.panelKind, .home)
        XCTAssertEqual(home.theme, 0)
        XCTAssertEqual(home.home.status, "READY  0.0 g")
        XCTAssertEqual(home.home.card.name, "Double")
        XCTAssertEqual(home.home.card.yieldLine, "18.0 g in -> 36.0 g out")
        XCTAssertEqual(home.home.scaleDot, "ok")
        XCTAssertEqual(home.home.scaleBattery, 74)
        XCTAssertTrue(home.home.canStart)
        XCTAssertNil(home.brew)
        let brew = try BrewAPI.decode(ScreenSnapshot.self, from: fixture("screen_brewing"))
        XCTAssertEqual(brew.panelKind, .brew)
        XCTAssertEqual(brew.brew?.phase, "BREWING")
        XCTAssertEqual(brew.brew?.running, true)
        XCTAssertEqual(brew.brew?.stop, "STOP")
        XCTAssertTrue(brew.live.firstDropSeen)
        let result = try BrewAPI.decode(ScreenSnapshot.self, from: fixture("screen_result"))
        XCTAssertEqual(result.panelKind, .result)
        XCTAssertEqual(result.result?.have, true)
        XCTAssertEqual(result.result?.shotId, 1)
        XCTAssertEqual(result.result?.line2.count, 3)
        XCTAssertEqual(result.result?.topTone, "accent")
    }

    func testLibrary() throws {
        let empty = try BrewAPI.decode(LibraryModel.self, from: fixture("library_idle"))
        XCTAssertEqual(empty.recipes.map(\.name), ["Double", "Single"])
        XCTAssertEqual(empty.presets.count, 2)
        XCTAssertEqual(empty.activePresetEntry?.recipeId, 1)
        XCTAssertEqual(empty.grinder.unit, "um")
        XCTAssertEqual(empty.grinder.dial, BrewFormat.Dial.microns)
        XCTAssertGreaterThan(empty.grinderModels.count, 10)
        XCTAssertNil(empty.otherGrinder)
        XCTAssertEqual(empty.recipe(id: 2)?.targetWeightG, 18)
        let shelf = try BrewAPI.decode(LibraryModel.self, from: fixture("library_coffee"))
        XCTAssertEqual(shelf.coffees.count, 1)
        let coffee = try XCTUnwrap(shelf.coffees.first)
        XCTAssertEqual(coffee.name, "Guji Natural")
        XCTAssertEqual(coffee.origin, "Ethiopia")
        XCTAssertEqual(coffee.altitudeM, 2100)
        XCTAssertEqual(coffee.roastDate, 20261101)
        XCTAssertFalse(coffee.isFinished)
        XCTAssertEqual(shelf.bags.count, 2)
        XCTAssertEqual(shelf.bags.first { $0.current }?.weightG, 250)
        XCTAssertEqual(shelf.bags.first { $0.current }?.id, coffee.bagId)
    }

    func testSettings() throws {
        let settings = try BrewAPI.decode(SettingsModel.self, from: fixture("settings_idle"))
        XCTAssertEqual(settings.uiTheme, 0)
        XCTAssertEqual(settings.theme, .dark)
        XCTAssertTrue(settings.diagnosticsOn)
        XCTAssertEqual(settings.recipes.count, 2)
        XCTAssertEqual(settings.recipes.first?.name, "Double")
        XCTAssertEqual(settings.grindToleranceS, 2)
        XCTAssertFalse(settings.net.webStartEnabled)
    }

    func testShotsAndShotData() throws {
        let none = try BrewAPI.decode([ShotModel].self, from: fixture("shots_idle"))
        XCTAssertTrue(none.isEmpty)
        let shots = try BrewAPI.decode([ShotModel].self, from: fixture("shots_result"))
        XCTAssertEqual(shots.count, 1)
        let shot = try XCTUnwrap(shots.first)
        XCTAssertEqual(shot.id, 1)
        XCTAssertEqual(shot.endReason, "weight")
        XCTAssertTrue(shot.stopConfirmed)
        XCTAssertTrue(shot.hasData)
        XCTAssertEqual(shot.finalWeightG, 37.2, accuracy: 0.001)
        XCTAssertEqual(shot.pourTimeS, 17.69, accuracy: 0.001)
        XCTAssertNotNil(shot.date)
        let data = try BrewAPI.decode(ShotDataModel.self, from: fixture("shot_data"))
        XCTAssertEqual(data.id, 1)
        XCTAssertEqual(data.shot.id, 1)
        XCTAssertEqual(data.data.samples, data.timeS.count)
        XCTAssertEqual(data.timeS.count, data.weightG.count)
        XCTAssertEqual(data.timeS.count, data.flowGS.count)
        XCTAssertEqual(data.timeS.count, data.scaleTimerS.count)
        XCTAssertTrue(data.analysis.valid)
        XCTAssertEqual(data.points.count, data.timeS.count)
    }

    func testShotsBusyAnswerIsAConflict() throws {
        let body = try fixture("shots_brewing")
        let response = BrewRawResponse(status: 409, contentType: "application/json", headers: [:], body: body)
        XCTAssertThrowsError(try BrewAPI().checked(response)) { error in
            XCTAssertEqual(error as? BrewAPIError, .http(status: 409, message: "not while a shot is running"))
        }
    }

    func testGrindDevicesPowerMaintenanceMachine() throws {
        let grind = try BrewAPI.decode(GrindAdviceModel.self, from: fixture("grind_idle"))
        XCTAssertEqual(grind.recipe, "Double")
        XCTAssertEqual(grind.action, "no_grind_set")
        XCTAssertEqual(grind.targetTimeS, 28)
        XCTAssertEqual(grind.confidence.score, 0.3, accuracy: 0.001)
        XCTAssertTrue(grind.room.on)
        let devices = try BrewAPI.decode(DevicesModel.self, from: fixture("devices_idle"))
        XCTAssertEqual(devices.scaleName, "LUNAR-1A45E2")
        XCTAssertEqual(devices.botLabel, "SwitchBot Bot")
        XCTAssertEqual(devices.botBattery, 87)
        XCTAssertEqual(devices.pairing.bot, "e5:8a:3c:11:9d:40")
        XCTAssertTrue(devices.botCaps.contains("press"))
        XCTAssertEqual(devices.seen.count, 2)
        XCTAssertEqual(devices.scaleData.battery, 74)
        let power = try BrewAPI.decode(PowerModel.self, from: fixture("power_idle"))
        XCTAssertFalse(power.paired)
        XCTAssertEqual(power.belief, "unknown")
        XCTAssertEqual(power.schedule.count, 6)
        XCTAssertEqual(power.schedule.first?.days, 127)
        let upkeep = try BrewAPI.decode(MaintenanceModel.self, from: fixture("maintenance_idle"))
        XCTAssertEqual(upkeep.jobs.map(\.id), ["backflush", "descale", "grinder_clean"])
        XCTAssertEqual(upkeep.jobs.first?.everyShots, 200)
        let machine = try BrewAPI.decode(MachineModel.self, from: fixture("machine_idle"))
        XCTAssertEqual(machine.current.maker, "Breville")
        XCTAssertEqual(machine.current.style, "toggle_button")
        XCTAssertEqual(machine.builtins.count, 6)
    }

    func testLenientDecoding() throws {
        // Unknown fields are ignored; missing ones fall back; a wrong type does not throw.
        let json = Data(#"{"state":"READY","future_field":{"x":1},"weight_g":"oops","scale_battery":null}"#.utf8)
        let status = try BrewAPI.decode(StatusModel.self, from: json)
        XCTAssertEqual(status.state, "READY")
        XCTAssertEqual(status.weightG, 0)
        XCTAssertNil(status.scaleBattery)
        let empty = try BrewAPI.decode(ScreenSnapshot.self, from: Data("{}".utf8))
        XCTAssertEqual(empty.panelKind, .home)
        XCTAssertNil(empty.grindPrompt)
    }
}

final class CoreThemeAndLiveTests: XCTestCase {
    func testPaletteTonesAndNames() {
        let palette = BrewPalette(rgb: [0x000000, 0xFFFFFF, 0x8A8A8A, 0x2ECC71, 0xE74C3C, 0x777777, 0xE67E22,
                                        0x1C1C1C, 0x2A2A2A, 0x3A2A12])
        XCTAssertEqual(palette.color(forTone: "ok"), palette.ok)
        XCTAssertEqual(palette.color(forTone: "bad"), palette.bad)
        XCTAssertEqual(palette.color(forTone: "accent"), palette.accent)
        XCTAssertEqual(palette.color(forTone: nil), palette.text)
        XCTAssertEqual(BrewThemeID.allCases.count, Int(bbw_palette_count()))
        XCTAssertEqual(BrewThemeID.solarizedDark.name, "Solarized Dark")
    }

    func testBrewLiveStateMapping() {
        var live = BrewLive()
        live.stateNumber = BrewState.brewing.rawValue
        XCTAssertTrue(live.isShotInHand)
        live.stateNumber = BrewState.ready.rawValue
        XCTAssertFalse(live.isShotInHand)
        XCTAssertTrue(BrewState.scaleDisconnected.isFault)
        XCTAssertFalse(BrewState.shotComplete.isFault)
    }
}
