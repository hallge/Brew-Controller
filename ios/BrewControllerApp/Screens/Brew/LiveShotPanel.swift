// The board's BREW panel: the shot ring (grey before the pump runs, red while it runs into an empty
// cup, green from the first drop, filling to the target), the target or live weight in the centre, the
// phase label, time, target and flow, and the STOP / CANCEL key. The words are the firmware's
// (`screen.brew.phase`, `.stop`); the numbers come from the 10 Hz live status so they never lag.
// STOP is `bbw_command`, straight away, never behind a request.
import SwiftUI

struct LiveShotPanel: View {
    var screen: ScreenSnapshot
    @State private var confirmStop = false

    private var live: BrewLive { LiveModel.shared.live }

    /// The machine has been started (the firmware says so; the state decides until the first answer).
    private var running: Bool {
        if let brew = screen.brew { return brew.running }
        switch live.state {
        case .startingShot, .brewing, .blooming, .stoppingShot, .postShotDrip: return true
        default: return false
        }
    }

    /// The centre shows the target until coffee is seen, then the live weight.
    private var showsWeight: Bool {
        live.firstDropSeen || live.state == .taring || live.state == .waitingForCup
    }

    private var progress: Double {
        guard running, live.firstDropSeen, live.targetWeightG > 0 else { return 0 }
        return min(1, max(0, live.weightG / live.targetWeightG))
    }

    private var ringColor: Color {
        if !running { return pal.track }
        return live.firstDropSeen ? pal.ok : pal.bad
    }

    /// The scale's own timer when it is valid, so the two displays agree.
    private var clock: Double {
        guard running else { return 0 }
        return live.scaleTimerValid && live.state == .brewing ? live.scaleTimerS : live.shotTimeS
    }

    private var stopLabel: String { screen.brew?.stop ?? "STOP" }
    private var needsConfirm: Bool { stopLabel == "STOP" }

    var body: some View {
        let phase = screen.brew?.phase ?? ""
        ScrollView {
            VStack(spacing: 20) {
                ZStack {
                    ShotRing(progress: running && live.firstDropSeen ? progress : (running ? 1 : 0),
                             color: ringColor, track: pal.track)
                        .accessibilityIdentifier("shotRing")
                    Text(showsWeight ? BrewFormat.grams(live.weightG) : BrewFormat.grams(live.targetWeightG))
                        .font(.system(size: 64, weight: .semibold, design: .rounded))
                        .monospacedDigit()
                        .foregroundStyle(showsWeight ? pal.ok : pal.dim)
                        .minimumScaleFactor(0.5)
                        .accessibilityIdentifier("weightLabel")
                }
                .frame(width: 270, height: 270)

                VStack(spacing: 6) {
                    Text(phase)
                        .font(.headline)
                        .multilineTextAlignment(.center)
                        .foregroundStyle(pal.color(forTone: screen.brew?.phaseTone))
                        .accessibilityIdentifier("phaseLabel")
                    Text(BrewFormat.clock(clock) + " s")
                        .font(.system(size: 40, weight: .semibold, design: .rounded)).monospacedDigit()
                        .foregroundStyle(pal.text)
                        .accessibilityIdentifier("timeLabel")
                    Text("/ \(BrewFormat.grams(live.targetWeightG)) g")
                        .font(.title3).foregroundStyle(pal.dim)
                        .accessibilityIdentifier("targetLabel")
                    Text(BrewFormat.flowUnit(live.flowGS))
                        .font(.title3).foregroundStyle(pal.text)
                        .accessibilityIdentifier("flowLabel")
                }

                if live.state == .readyToBrew && live.botConnected {
                    Button("START") { LiveModel.shared.send(.start) }
                        .buttonStyle(BrewKeyStyle(kind: .ok))
                        .accessibilityIdentifier("startButton")
                }
                HStack(spacing: 10) {
                    if live.scaleConnected && !running {
                        Button("TARE") { LiveModel.shared.send(.tare) }
                            .buttonStyle(BrewKeyStyle(kind: .panel))
                            .accessibilityIdentifier("tareButton")
                    }
                    Button(stopLabel) {
                        if needsConfirm { confirmStop = true } else { LiveModel.shared.send(.stop) }
                    }
                    .buttonStyle(BrewKeyStyle(kind: .bad))
                    .accessibilityIdentifier("stopButton")
                }
            }
            .padding(16)
            .frame(maxWidth: .infinity)
        }
        .accessibilityIdentifier("brewPanel")
        .alert("STOP THE SHOT?", isPresented: $confirmStop) {
            // Not queued behind anything: bbw_command goes straight to the controller.
            Button("STOP", role: .destructive) { LiveModel.shared.send(.stop) }
                .accessibilityIdentifier("confirmStopButton")
            Button("KEEP BREWING", role: .cancel) {}
                .accessibilityIdentifier("keepBrewingButton")
        }
    }
}
