import XCTest

/// The same run as test/hostapp/hostapp_main.cpp, through the C API from Swift: start the controller in
/// demo mode on a temporary folder, pair the simulated bot, pick the Breville profile, put the cup on
/// the scale, start, wait for the shot to be stored, and check the record.
///
/// The controller starts once per process and never stops, so everything is one test method.
final class DemoShotTests: XCTestCase {
    struct Reply {
        var status = 0
        var body = ""
    }

    /// One in-process web request. Blocks until the controller answers.
    private func call(_ method: String, _ path: String, _ body: String = "") -> Reply {
        let bytes = Array(body.utf8)
        var response = BbwResponse()
        let rc = bytes.withUnsafeBytes { raw in
            bbw_request(method, path, raw.baseAddress, bytes.count, body.isEmpty ? nil : "application/json", 15000, &response)
        }
        guard rc == 0 else { return Reply() }
        defer { bbw_response_free(&response) }
        let data = Data(bytes: response.body, count: response.body_len)
        return Reply(status: Int(response.status), body: String(decoding: data, as: UTF8.self))
    }

    private func name(_ live: BbwLive) -> String {
        withUnsafeBytes(of: live.state_name) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
    }

    private func live() -> BbwLive {
        var value = BbwLive()
        bbw_live(&value)
        return value
    }

    /// Polls every 100 ms until `predicate` holds or `seconds` pass.
    private func wait(_ seconds: Double, _ predicate: (BbwLive) -> Bool) async throws -> Bool {
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            if predicate(live()) { return true }
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        return predicate(live())
    }

    func testDemoShotThroughTheCApi() async throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("bbw-test-\(UUID().uuidString)")
        bbw_ios_prepare_at(dir.path, 1)
        XCTAssertEqual(bbw_host_start(), 0, "bbw_host_start")

        var ready = false
        for _ in 0..<300 where !ready {
            ready = bbw_host_ready() != 0
            if !ready { try await Task.sleep(nanoseconds: 100_000_000) }
        }
        XCTAssertTrue(ready, "setup() finished")
        guard ready else { return }

        // The simulated bot's address is fixed in emu/src/emu_devices.cpp.
        XCTAssertEqual(call("POST", "/api/pair", #"{"kind":"bot","address":"e5:8a:3c:11:9d:40","type":1}"#).status, 200)
        // Right after start-up the controller can answer 409 for a moment: retry for a few seconds.
        var picked = call("POST", "/api/machine", #"{"builtin":1}"#)
        for _ in 0..<40 where picked.status == 409 {
            try await Task.sleep(nanoseconds: 250_000_000)
            picked = call("POST", "/api/machine", #"{"builtin":1}"#)
        }
        XCTAssertEqual(picked.status, 200)
        let machine = call("GET", "/api/machine")
        XCTAssertTrue(machine.status == 200 && machine.body.contains("Breville"), "machine is the Breville")

        let linked = try await wait(60) { $0.scale_connected != 0 && $0.bot_connected != 0 }
        XCTAssertTrue(linked, "scale and bot connected")

        // The cup goes on the scale, toggled so the firmware sees it arrive.
        _ = bbw_demo_set("machine", "cupOnScale", "0")
        try await Task.sleep(nanoseconds: 3_000_000_000)
        XCTAssertEqual(bbw_demo_set("machine", "cupOnScale", "1"), 1)
        let cup = try await wait(10) { $0.cup_present != 0 }
        XCTAssertTrue(cup, "cup seen on the scale")
        _ = try await wait(20) { self.name($0) == "READY" }

        let before = live().last_shot_id
        XCTAssertEqual(bbw_command(Int32(BBW_CMD_START)), 0)
        let stored = try await wait(120) { $0.last_shot_id != before }
        XCTAssertTrue(stored, "the shot ended and was stored")
        try await Task.sleep(nanoseconds: 500_000_000)

        let shots = call("GET", "/api/shots?limit=1")
        XCTAssertEqual(shots.status, 200)
        let json = try JSONSerialization.jsonObject(with: Data(shots.body.utf8)) as? [[String: Any]]
        XCTAssertEqual(json?.count, 1, "one record")
        XCTAssertEqual(json?.first?["end_reason"] as? String, "weight")
        XCTAssertEqual(json?.first?["stop_confirmed"] as? Bool, true)

        _ = bbw_command(Int32(BBW_CMD_ACK))
        for path in ["/api/screen", "/api/library", "/api/settings"] {
            XCTAssertEqual(call("GET", path).status, 200, path)
        }
    }
}
