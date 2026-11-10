import XCTest

/// A full simulated shot, run through the bridge on a fake clock. The simulator is the host tests'
/// machine (Sim/MachineSim.h); its Lunar sends real Acaia frames through the core's ScaleLink.
final class BridgeShotTests: XCTestCase {
    private var core: CoreSession!
    private var now: UInt32 = 100_000

    override func setUp() {
        super.setUp()
        core = CoreSession.create()
        now = 100_000
    }

    private func stateName() -> String { String(cString: core.live(now).stateName) }

    /// Ticks in 10 ms steps until `done`, at most `maxMs` of simulated time.
    @discardableResult
    private func run(maxMs: UInt32, until done: () -> Bool) -> Bool {
        var elapsed: UInt32 = 0
        while elapsed < maxMs {
            now += 10
            core.tick(now)
            elapsed += 10
            if done() { return true }
        }
        return false
    }

    private func reachReady() {
        XCTAssertTrue(run(maxMs: 3_000) { self.stateName() == "READY" }, "no READY, state \(stateName())")
    }

    func testFullShotReachesShotComplete() {
        reachReady()
        core.requestStart()
        XCTAssertTrue(run(maxMs: 60_000) { self.stateName() == "SHOT_COMPLETE" },
                      "no SHOT_COMPLETE, state \(stateName())")

        let shot = core.lastShot()
        XCTAssertTrue(shot.valid)
        XCTAssertEqual(core.live(now).completedShots, 1)
        XCTAssertEqual(Double(shot.targetWeightG), 36.0, accuracy: 0.01)
        XCTAssertEqual(Double(shot.finalWeightG), 36.0, accuracy: 1.5)
        XCTAssertLessThan(shot.stopWeightG, shot.targetWeightG)   // stopped early on purpose
        XCTAssertGreaterThan(shot.finalWeightG, shot.stopWeightG) // and the drip was observed
        XCTAssertGreaterThan(Double(shot.shotTimeS), 20)
        XCTAssertLessThan(Double(shot.shotTimeS), 35)
        // The shot ended on weight: the stop was by the reading (direct or predicted), not a time-out.
        let reason = String(cString: CoreSession.endReasonText(shot.endReason))
        XCTAssertTrue(reason == "weight" || reason == "weight_predicted", "ended: \(reason)")
    }

    func testRecipeTargetIsUsed() {
        core.setRecipe(30, 16)
        XCTAssertEqual(Double(core.recipeTargetG()), 30, accuracy: 0.001)
        reachReady()
        core.requestStart()
        XCTAssertTrue(run(maxMs: 60_000) { self.stateName() == "SHOT_COMPLETE" })
        XCTAssertEqual(Double(core.lastShot().finalWeightG), 30.0, accuracy: 1.5)
        XCTAssertEqual(Double(core.lastShot().doseG), 16, accuracy: 0.001)
    }

    func testMissedStopStillEnds() {
        reachReady()
        core.setFault(Fault.missedStop, true)
        XCTAssertTrue(core.faultActive(Fault.missedStop))
        core.requestStart()
        XCTAssertTrue(run(maxMs: 90_000) { self.stateName() == "SHOT_COMPLETE" || self.core.live(self.now).fault },
                      "state \(stateName())")
        // The first stop press did nothing, so the cup took more than the target.
        if core.lastShot().valid { XCTAssertGreaterThan(core.lastShot().finalWeightG, 36.0) }
    }

    func testSuspendedTicksShowInTheProbeLog() {
        reachReady()
        core.requestStart()
        XCTAssertTrue(run(maxMs: 30_000) { self.stateName() == "BREWING" })
        core.pauseTicks(4_000, now)
        XCTAssertTrue(core.live(now).ticksPaused)
        XCTAssertTrue(run(maxMs: 60_000) { self.stateName() == "SHOT_COMPLETE" || self.core.live(self.now).fault })
        XCTAssertFalse(core.live(now).ticksPaused)

        // After the pause the core was ticked with a jump of about 4 s.
        var longestGap: Float = 0
        var sawResume = false
        var seq: UInt32 = 1
        while seq <= core.probeTotal() {
            let e = core.probeAt(seq)
            if e.kind == Probe.tick { longestGap = max(longestGap, e.a) }
            if e.kind == Probe.note && e.c == 2 { sawResume = true }
            seq += 1
        }
        XCTAssertGreaterThanOrEqual(longestGap, 3_900)
        XCTAssertTrue(sawResume)
    }

    func testSilentScaleLeavesReady() {
        reachReady()
        core.setFault(Fault.scaleSilent, true)
        XCTAssertTrue(run(maxMs: 10_000) { self.stateName() != "READY" }, "still \(stateName())")
    }

    func testProbeLogCountsTicksSamplesAndCommands() {
        reachReady()
        core.requestStart()
        XCTAssertTrue(run(maxMs: 60_000) { self.stateName() == "SHOT_COMPLETE" })
        var kinds: [UInt8: Int] = [:]
        var seq: UInt32 = 1
        while seq <= core.probeTotal() {
            kinds[core.probeAt(seq).kind, default: 0] += 1
            seq += 1
        }
        XCTAssertGreaterThan(kinds[Probe.tick] ?? 0, 1_000)
        XCTAssertGreaterThan(kinds[Probe.sample] ?? 0, 100)
        XCTAssertGreaterThanOrEqual(kinds[Probe.command] ?? 0, 3)   // tare, a press to start, a press to stop
        XCTAssertGreaterThan(kinds[Probe.state] ?? 0, 5)
    }
}
