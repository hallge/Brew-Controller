// Unit tests for the Swift core layer (ios/BrewControllerApp/Core). The model tests decode JSON
// captured from the real routes (Fixtures/, written by `test/hostapp/run.sh --dump DIR`).
import XCTest
@testable import BrewController

final class CoreFormatTests: XCTestCase {
    func testWeightTimeFlowRatio() {
        XCTAssertEqual(BrewFormat.grams(36), "36.0")
        XCTAssertEqual(BrewFormat.gramsUnit(18.04), "18.0 g")
        XCTAssertEqual(BrewFormat.seconds(27.46), "27.5")
        XCTAssertEqual(BrewFormat.secondsUnit(5), "5.0 s")
        XCTAssertEqual(BrewFormat.flow(1.254), "1.25")
        XCTAssertEqual(BrewFormat.flowUnit(0.5), "0.50 g/s")
        XCTAssertEqual(BrewFormat.ratio(2), "1:2.0")
        XCTAssertEqual(BrewFormat.decimal(-0.04, 1), "0.0")
        XCTAssertEqual(BrewFormat.battery(-1), "--")
        XCTAssertEqual(BrewFormat.battery(85), "85%")
        XCTAssertEqual(BrewFormat.clock(27.5), "27.5")
        XCTAssertEqual(BrewFormat.clock(65), "1:05.0")
    }

    func testGrindDials() {
        let plain = BrewFormat.Dial(unit: "um")
        XCTAssertEqual(plain.setting(246), "246")
        XCTAssertEqual(plain.settingUnit(246), "246 um")
        XCTAssertEqual(plain.settingUnit(0), "--")
        let halves = BrewFormat.Dial(unit: "clicks", divisor: 2)
        XCTAssertEqual(halves.setting(7), "3.5")
        let quarters = BrewFormat.Dial(unit: "turns", divisor: 4)
        XCTAssertEqual(quarters.setting(17), "4.25")
        let ringed = BrewFormat.Dial(unit: "", divisor: 1, subSteps: 10)
        XCTAssertEqual(ringed.setting(1), "1:1")
        XCTAssertEqual(ringed.setting(23), "3:3")
        XCTAssertEqual(ringed.deltaUnit, "steps")
        XCTAssertEqual(halves.signedDelta(-3), "-1.5 clicks")
        XCTAssertEqual(plain.signedDelta(5), "+5 um")
    }
}

final class CoreAPIHelperTests: XCTestCase {
    func testQueryAndFormEncoding() {
        XCTAssertEqual(BrewAPI.pathWithQuery("/api/shots", [:]), "/api/shots")
        XCTAssertEqual(BrewAPI.pathWithQuery("/api/shots", ["limit": "20", "a b": "c&d"]),
                       "/api/shots?a%20b=c%26d&limit=20")
        XCTAssertEqual(BrewAPI.pathWithQuery("/x?y=1", ["z": "2"]), "/x?y=1&z=2")
        XCTAssertEqual(BrewAPI.formEncoded(["name": "A B"]), "name=A%20B")
    }

    func testFilenameFromContentDisposition() {
        XCTAssertEqual(BrewAPI.filename(fromContentDisposition: "attachment; filename=\"shots.csv\""), "shots.csv")
        XCTAssertEqual(BrewAPI.filename(fromContentDisposition: "attachment; filename=backup.bin"), "backup.bin")
        XCTAssertEqual(BrewAPI.filename(fromContentDisposition: "attachment; filename*=UTF-8''a%20b.csv"), "a b.csv")
        XCTAssertNil(BrewAPI.filename(fromContentDisposition: nil))
        XCTAssertNil(BrewAPI.filename(fromContentDisposition: "inline"))
    }

    func testHeaderParsing() {
        let headers = BrewAPI.parseHeaders("Content-Disposition: attachment; filename=\"a.csv\"\nX-Log-Next: 12\n")
        XCTAssertEqual(headers["X-Log-Next"], "12")
        let response = BrewRawResponse(status: 200, contentType: "text/csv", headers: headers, body: Data())
        XCTAssertEqual(response.header("content-disposition"), "attachment; filename=\"a.csv\"")
    }

    func testJSONBodyEncoding() throws {
        let data = try BrewAPI.encode(["uiTheme": .int(2), "on": .bool(true), "name": "x"])
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(object["uiTheme"] as? Int, 2)
        XCTAssertEqual(object["on"] as? Bool, true)
        XCTAssertEqual(object["name"] as? String, "x")
    }

    func testErrorMapping() throws {
        let api = BrewAPI()
        let bad = BrewRawResponse(status: 409, contentType: "application/json", headers: [:],
                                  body: Data(#"{"ok":false,"error":"not while a shot is running"}"#.utf8))
        XCTAssertThrowsError(try api.checked(bad)) { error in
            XCTAssertEqual(error as? BrewAPIError, .http(status: 409, message: "not while a shot is running"))
            XCTAssertTrue((error as? BrewAPIError)?.isConflict ?? false)
        }
        let rejected = BrewRawResponse(status: 200, contentType: "application/json", headers: [:],
                                       body: Data(#"{"ok":false,"error":"nope"}"#.utf8))
        XCTAssertThrowsError(try api.checked(rejected)) { XCTAssertEqual($0 as? BrewAPIError, .rejected(message: "nope")) }
        let timeout = BrewRawResponse(status: 503, contentType: "", headers: [:], body: Data())
        XCTAssertThrowsError(try api.checked(timeout)) { XCTAssertEqual($0 as? BrewAPIError, .timeout) }
        let ok = BrewRawResponse(status: 200, contentType: "application/json", headers: [:], body: Data(#"{"ok":true}"#.utf8))
        XCTAssertNoThrow(try api.checked(ok))
    }
}

@MainActor
final class CoreOnboardingTests: XCTestCase {
    private func makeDefaults() -> UserDefaults {
        let name = "bbw.tests." + UUID().uuidString
        let defaults = UserDefaults(suiteName: name)!
        defaults.removePersistentDomain(forName: name)
        return defaults
    }

    func testStepsInOrder() {
        let onboarding = Onboarding(defaults: makeDefaults())
        onboarding.bluetoothReader = { .poweredOff }
        XCTAssertEqual(onboarding.step, .safetyNotice)
        onboarding.acceptSafetyNotice()
        XCTAssertEqual(onboarding.step, .chooseMode)
        onboarding.chooseMode(.real)
        XCTAssertEqual(onboarding.step, .bluetooth)
        onboarding.bluetoothStepFinished()
        XCTAssertEqual(onboarding.step, .diagnostics)
        XCTAssertFalse(onboarding.isComplete)
    }

    func testDemoSkipsBluetoothAndPersists() {
        let defaults = makeDefaults()
        let first = Onboarding(defaults: defaults)
        first.acceptSafetyNotice()
        first.chooseMode(.demo)
        XCTAssertEqual(first.step, .diagnostics)
        XCTAssertEqual(first.dataFolderName, "Controller-demo")
        let second = Onboarding(defaults: defaults)
        XCTAssertEqual(second.mode, .demo)
        XCTAssertTrue(second.safetyAccepted)
        XCTAssertTrue(second.switchMode(.real))
        XCTAssertFalse(second.switchMode(.real))
    }
}

final class CoreRetryTests: XCTestCase {
    func testRetryableErrors() {
        XCTAssertTrue(RetryingTask<Int>.isRetryable(.notReady))
        XCTAssertTrue(RetryingTask<Int>.isRetryable(.timeout))
        XCTAssertTrue(RetryingTask<Int>.isRetryable(.http(status: 500, message: nil)))
        XCTAssertFalse(RetryingTask<Int>.isRetryable(.http(status: 409, message: nil)))
        XCTAssertFalse(RetryingTask<Int>.isRetryable(.decoding("x")))
    }
}
