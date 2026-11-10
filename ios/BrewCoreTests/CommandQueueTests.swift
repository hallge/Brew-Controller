import XCTest

/// The queue that carries the core's writes to Swift (BrewCoreBridge/BridgeTypes.h).
final class CommandQueueTests: XCTestCase {
    private func command(_ kind: UInt8, _ first: UInt8, length: UInt8 = 3) -> BridgeCommand {
        var c = BridgeCommand()
        c.kind = kind
        c.length = length
        c.atMs = UInt32(first)
        c.setByte(0, first)
        return c
    }

    func testFirstInFirstOut() {
        var q = CommandQueue()
        XCTAssertEqual(q.size(), 0)
        XCTAssertTrue(q.push(command(Cmd.writeScale, 1)))
        XCTAssertTrue(q.push(command(Cmd.writeBot, 2)))
        XCTAssertTrue(q.push(command(Cmd.writeScale, 3)))
        XCTAssertEqual(q.size(), 3)

        let a = q.pop(), b = q.pop(), c = q.pop()
        XCTAssertEqual([a.byte(0), b.byte(0), c.byte(0)], [1, 2, 3])
        XCTAssertEqual([a.kind, b.kind, c.kind], [Cmd.writeScale, Cmd.writeBot, Cmd.writeScale])
        XCTAssertEqual(a.length, 3)
        XCTAssertEqual(q.size(), 0)
    }

    func testEmptyQueueAnswersNone() {
        var q = CommandQueue()
        XCTAssertEqual(q.pop().kind, 0)
        _ = q.push(command(Cmd.writeBot, 9))
        _ = q.pop()
        XCTAssertEqual(q.pop().kind, 0)
    }

    func testFullQueueRefusesTheNewCommandAndCounts() {
        var q = CommandQueue()
        for i in 0..<64 { XCTAssertTrue(q.push(command(Cmd.writeBot, UInt8(i)))) }
        XCTAssertFalse(q.push(command(Cmd.writeBot, 200)))
        XCTAssertFalse(q.push(command(Cmd.writeBot, 201)))
        XCTAssertEqual(q.size(), 64)
        XCTAssertEqual(q.dropped, 2)
        XCTAssertEqual(q.pushed, 64)
        // Nothing old was dropped: the order is intact.
        XCTAssertEqual(q.pop().byte(0), 0)
        XCTAssertEqual(q.pop().byte(0), 1)
    }

    func testWrapsAround() {
        var q = CommandQueue()
        for round in 0..<5 {
            for i in 0..<40 { XCTAssertTrue(q.push(command(Cmd.writeScale, UInt8(i)))) }
            for i in 0..<40 { XCTAssertEqual(q.pop().byte(0), UInt8(i), "round \(round)") }
        }
        XCTAssertEqual(q.dropped, 0)
    }

    func testByteAccessIsBounded() {
        var c = BridgeCommand()
        c.setByte(19, 0xAB)
        c.setByte(20, 0xCD)  // past the end: ignored
        XCTAssertEqual(c.byte(19), 0xAB)
        XCTAssertEqual(c.byte(20), 0)
    }

    /// Through the session, as the transport uses it: a scale link coming up queues the scale init
    /// writes, in order, as writes to the scale (an Acaia identifies itself with ef dd ...).
    func testSessionQueuesTheScaleInitWrites() {
        let core = CoreSession.create()
        core.useSimulator(false)
        XCTAssertEqual(core.pendingCommands(), 0)
        core.scaleLink(true, "acaia", 5_000)
        XCTAssertGreaterThanOrEqual(core.pendingCommands(), 2)
        let first = core.popCommand()
        XCTAssertEqual(first.kind, Cmd.writeScale)
        XCTAssertEqual(first.byte(0), 0xEF)
        XCTAssertEqual(first.byte(1), 0xDD)
        XCTAssertEqual(core.droppedCommands(), 0)
    }
}
