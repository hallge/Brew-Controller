import Foundation
import Observation

/// What the screen shows, copied out of the C++ LiveView on the core queue.
struct LiveState: Equatable {
    var stateName = "BOOT"
    var fault = false
    var weightG = 0.0
    var flowGS = 0.0
    var shotTimeS = 0.0
    var targetG = 0.0
    var scaleConnected = false
    var botConnected = false
    var botWanted = false
    var ticksPaused = false
    var completedShots = 0

    enum Action { case start, stop, done, none }

    var action: Action {
        switch stateName {
        case "READY": return .start
        case "TARING", "WAITING_FOR_CUP", "READY_TO_BREW", "STARTING_SHOT", "BREWING", "BLOOMING",
             "STOPPING_SHOT", "POST_SHOT_DRIP":
            return .stop
        case "SHOT_COMPLETE": return .done
        default: return fault ? .done : .none
        }
    }
}

struct ShotResult: Equatable {
    var id = 0
    var targetG = 0.0
    var finalG = 0.0
    var stopG = 0.0
    var dripG = 0.0
    var shotTimeS = 0.0
    var doseG = 0.0
    var stopLatencyMs = 0
    var endReason = ""
}

struct ProbeLine: Identifiable, Equatable {
    var id: UInt32
    var kind: UInt8
    var text: String
}

struct ProbeStats: Equatable {
    var ticks = 0
    var samples = 0
    var commands = 0
    var maxTickGapMs = 0
}

/// Runs the core. Everything that touches `CoreSession` happens on `queue`, which is also the queue
/// CoreBluetooth is given; the UI only ever sees copies published on the main thread.
@Observable
final class ShotSession {
    var live = LiveState()
    var lastShot: ShotResult?
    var recipeTargetG = 36.0
    var recipeDoseG = 18.0
    var realDevices = false
    var transportStatus = "Simulated machine"
    var lostAckArmed = false
    var missedStopArmed = false
    var scaleSilent = false
    var probeLines: [ProbeLine] = []
    var probeStats = ProbeStats()

    @ObservationIgnored private let core: CoreSession
    @ObservationIgnored private let queue = DispatchQueue(label: "com.hallge.brewcontroller.core",
                                                          qos: .userInteractive)
    @ObservationIgnored private var timer: DispatchSourceTimer?
    @ObservationIgnored private var transport: CentralController?
    @ObservationIgnored private var tickCount = 0
    @ObservationIgnored private var publishedShots = 0
    @ObservationIgnored private var coreState = LiveState()  // the core queue's own copy
    @ObservationIgnored private let bootMs: UInt32

    init() {
        core = CoreSession.create()
        bootMs = CoreSession.uptimeMs()
        recipeTargetG = Double(core.recipeTargetG())
        recipeDoseG = Double(core.recipeDoseG())
        startTimer()
    }

    deinit {
        timer?.cancel()
    }

    // MARK: Timer

    private func startTimer() {
        let source = DispatchSource.makeTimerSource(queue: queue)
        source.schedule(deadline: .now(), repeating: .milliseconds(10), leeway: .milliseconds(1))
        source.setEventHandler { [weak self] in self?.onTimer() }
        source.resume()
        timer = source
    }

    /// On the core queue.
    private func onTimer() {
        let now = CoreSession.uptimeMs()
        core.tick(now)
        transport?.drain()
        tickCount &+= 1
        if tickCount % 5 == 0 { publish(now) }
    }

    /// On the core queue.
    private func publish(_ now: UInt32) {
        let v = core.live(now)
        var state = LiveState()
        state.stateName = String(cString: v.stateName)
        state.fault = v.fault
        state.weightG = Double(v.weightG)
        state.flowGS = Double(v.flowGS)
        state.shotTimeS = Double(v.shotTimeS)
        state.targetG = Double(v.targetG)
        state.scaleConnected = v.scaleConnected
        state.botConnected = v.botConnected
        state.botWanted = v.botWanted
        state.ticksPaused = v.ticksPaused
        state.completedShots = Int(v.completedShots)
        coreState = state

        var shot: ShotResult?
        if state.completedShots != publishedShots {
            publishedShots = state.completedShots
            let s = core.lastShot()
            if s.valid {
                shot = ShotResult(id: Int(s.id), targetG: Double(s.targetWeightG),
                                  finalG: Double(s.finalWeightG), stopG: Double(s.stopWeightG),
                                  dripG: Double(s.dripWeightG), shotTimeS: Double(s.shotTimeS),
                                  doseG: Double(s.doseG), stopLatencyMs: Int(s.stopLatencyMs),
                                  endReason: String(cString: CoreSession.endReasonText(s.endReason)))
            }
        }
        let lost = core.faultActive(Fault.lostAck)
        let missed = core.faultActive(Fault.missedStop)
        let silent = core.faultActive(Fault.scaleSilent)
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            if self.live != state { self.live = state }
            if let shot { self.lastShot = shot }
            if self.lostAckArmed != lost { self.lostAckArmed = lost }
            if self.missedStopArmed != missed { self.missedStopArmed = missed }
            if self.scaleSilent != silent { self.scaleSilent = silent }
        }
    }

    // MARK: Intents

    func primaryAction() {
        queue.async { [self] in
            switch coreState.action {
            case .start: core.requestStart()
            case .stop: core.requestStop()
            case .done: core.acknowledge()
            case .none: break
            }
        }
    }

    func tare() {
        queue.async { [self] in core.requestTare() }
    }

    func setRecipe(targetG: Double, doseG: Double) {
        queue.async { [self] in
            core.setRecipe(Float(targetG), Float(doseG))
            let t = Double(core.recipeTargetG()), d = Double(core.recipeDoseG())
            DispatchQueue.main.async { [self] in
                recipeTargetG = t
                recipeDoseG = d
            }
        }
    }

    // MARK: Faults

    func setFault(_ fault: UInt8, _ on: Bool) {
        queue.async { [self] in core.setFault(fault, on) }
    }

    func pauseTicks(seconds: Int) {
        queue.async { [self] in core.pauseTicks(UInt32(seconds * 1000), CoreSession.uptimeMs()) }
    }

    // MARK: Real devices (untested)

    func setRealDevices(_ on: Bool) {
        realDevices = on
        transportStatus = on ? "Starting Bluetooth…" : "Simulated machine"
        queue.async { [self] in
            if on {
                core.useSimulator(false)
                let t = CentralController(core: core, queue: queue)
                t.onStatus = { [weak self] text in
                    DispatchQueue.main.async { self?.transportStatus = text }
                }
                transport = t
                t.start()
            } else {
                transport?.stop()
                transport = nil
                core.useSimulator(true)
            }
        }
    }

    // MARK: Probe log

    /// Reads the newest entries of the core's probe log (call while the log sheet is open).
    func refreshProbe(limit: Int = 300) {
        queue.async { [self] in
            let total = core.probeTotal()
            var lines: [ProbeLine] = []
            var stats = ProbeStats()
            // Statistics over the last ~2000 entries, the lines over the last `limit` non-tick ones
            // plus ticks, newest first.
            let statsFrom = total > 2000 ? total - 2000 + 1 : 1
            if total >= statsFrom {
                for seq in statsFrom...total {
                    let e = core.probeAt(seq)
                    if e.seq == 0 { continue }
                    switch e.kind {
                    case Probe.tick:
                        stats.ticks += 1
                        stats.maxTickGapMs = max(stats.maxTickGapMs, Int(e.a))
                    case Probe.sample: stats.samples += 1
                    case Probe.command: stats.commands += 1
                    default: break
                    }
                }
            }
            var seq = total
            while seq >= 1 && lines.count < limit {
                let e = core.probeAt(seq)
                if e.seq != 0 { lines.append(ProbeLine(id: e.seq, kind: e.kind, text: describe(e))) }
                seq -= 1
            }
            DispatchQueue.main.async { [self] in
                probeLines = lines
                probeStats = stats
            }
        }
    }

    private func describe(_ e: ProbeEntry) -> String {
        let t = String(format: "%8.3f", Double(Int64(e.atMs) - Int64(bootMs)) / 1000.0)
        switch e.kind {
        case Probe.tick:
            return "\(t)  tick  +\(Int(e.a)) ms  \(String(cString: CoreSession.stateText(UInt8(e.c))))"
        case Probe.sample:
            return "\(t)  sample  \(String(format: "%.1f", e.a)) g  #\(e.c)"
        case Probe.command:
            let target = e.c == UInt32(Cmd.writeScale) ? "scale" : "bot"
            if e.a > 0 {
                return "\(t)  cmd → \(target)  \(Int(e.a)) bytes, first 0x\(String(Int(e.b), radix: 16))"
            }
            return "\(t)  cmd → \(target)  simulated (op \(Int(e.b)))"
        case Probe.state:
            return "\(t)  state → \(String(cString: CoreSession.stateText(UInt8(e.c))))"
        default:
            switch e.c {
            case 1: return "\(t)  ticks paused for \(String(format: "%.0f", e.a)) s"
            case 2: return "\(t)  ticks resumed after \(String(format: "%.1f", e.a)) s"
            case 3: return "\(t)  fault \(Int(e.b)) \(e.a > 0 ? "on" : "off")"
            case 4: return "\(t)  request \(["", "start", "stop", "tare", "acknowledge"][min(Int(e.b), 4)])"
            case 5: return "\(t)  command queue full"
            default: return "\(t)  note \(e.c)"
            }
        }
    }
}
