import SwiftUI
import UIKit

/// The one main screen: a ring like README §1, the weight, the time, the state, Start/Stop.
struct MainView: View {
    let session: ShotSession
    @Environment(\.scenePhase) private var scenePhase
    @State private var sheet: Sheet?

    enum Sheet: String, Identifiable {
        case recipe, faults, probe, devices
        var id: String { rawValue }
    }

    var body: some View {
        let live = session.live
        VStack(spacing: 20) {
            banner
            Spacer(minLength: 0)
            ring(live)
                .frame(maxWidth: 340, maxHeight: 340)
                .padding(.horizontal, 24)
            Text(live.stateName)
                .font(.system(.title3, design: .monospaced).weight(.semibold))
                .foregroundStyle(live.fault ? Color.red : Color.primary)
                .accessibilityIdentifier("stateLabel")
            if live.ticksPaused {
                Text("Ticks paused (imitating a suspended app)")
                    .font(.footnote)
                    .foregroundStyle(.orange)
            }
            primaryButton(live)
            if let shot = session.lastShot, live.stateName == "SHOT_COMPLETE" || live.stateName == "READY" {
                resultCard(shot)
            }
            Spacer(minLength: 0)
            toolbar
        }
        .padding(.vertical)
        .sheet(item: $sheet) { which in
            switch which {
            case .recipe: RecipeSheet(session: session)
            case .faults: FaultsSheet(session: session)
            case .probe: ProbeLogSheet(session: session)
            case .devices: DevicesSheet(session: session)
            }
        }
        // The idle timer is off while the app is open: a locked screen is a suspended app.
        .onAppear { UIApplication.shared.isIdleTimerDisabled = true }
        .onDisappear { UIApplication.shared.isIdleTimerDisabled = false }
        .onChange(of: scenePhase) { _, phase in
            UIApplication.shared.isIdleTimerDisabled = (phase == .active)
        }
    }

    private var banner: some View {
        Text(session.realDevices ? "Real devices (untested)" : "Simulated machine")
            .font(.footnote.weight(.medium))
            .padding(.horizontal, 10).padding(.vertical, 4)
            .background(session.realDevices ? Color.orange.opacity(0.25) : Color.secondary.opacity(0.15),
                        in: Capsule())
    }

    private func ring(_ live: LiveState) -> some View {
        let progress = live.targetG > 0 ? min(max(live.weightG / live.targetG, 0), 1) : 0
        return ZStack {
            Circle().stroke(Color.secondary.opacity(0.2), lineWidth: 22)
            Circle()
                .trim(from: 0, to: progress)
                .stroke(live.fault ? Color.red : Color.accentColor,
                        style: StrokeStyle(lineWidth: 22, lineCap: .round))
                .rotationEffect(.degrees(-90))
                .animation(.linear(duration: 0.1), value: progress)
            VStack(spacing: 4) {
                Text(String(format: "%.1f g", live.weightG))
                    .font(.system(size: 54, weight: .semibold, design: .rounded))
                    .monospacedDigit()
                    .minimumScaleFactor(0.6)
                    .accessibilityIdentifier("weightLabel")
                Text(String(format: "%.1f s", live.shotTimeS))
                    .font(.title2.monospacedDigit())
                    .accessibilityIdentifier("timeLabel")
                Text(String(format: "target %.0f g", live.targetG))
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
    }

    private func primaryButton(_ live: LiveState) -> some View {
        let (title, tint): (String, Color) = {
            switch live.action {
            case .start: return ("Start", .green)
            case .stop: return ("Stop", .red)
            case .done: return (live.fault ? "Clear" : "Done", .blue)
            case .none: return ("Wait", .gray)
            }
        }()
        return Button {
            session.primaryAction()
        } label: {
            Text(title)
                .font(.title2.weight(.semibold))
                .frame(maxWidth: 260, minHeight: 56)
        }
        .buttonStyle(.borderedProminent)
        .tint(tint)
        .disabled(live.action == .none)
        .accessibilityIdentifier("primaryButton")
    }

    private func resultCard(_ shot: ShotResult) -> some View {
        VStack(spacing: 2) {
            Text("Shot \(shot.id): \(String(format: "%.1f", shot.finalG)) g in \(String(format: "%.1f", shot.shotTimeS)) s")
                .font(.subheadline.weight(.medium))
            Text("target \(String(format: "%.0f", shot.targetG)) g, stopped at \(String(format: "%.1f", shot.stopG)) g, ended: \(shot.endReason)")
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("resultCard")
    }

    private var toolbar: some View {
        HStack(spacing: 12) {
            toolbarButton("Recipe", "slider.horizontal.3", .recipe, "recipeButton")
            toolbarButton("Faults", "exclamationmark.triangle", .faults, "faultsButton")
            toolbarButton("Probe", "waveform.path.ecg", .probe, "probeButton")
            toolbarButton("Devices", "antenna.radiowaves.left.and.right", .devices, "devicesButton")
        }
        .padding(.horizontal)
    }

    private func toolbarButton(_ title: String, _ symbol: String, _ which: Sheet, _ id: String) -> some View {
        Button {
            sheet = which
        } label: {
            VStack(spacing: 2) {
                Image(systemName: symbol)
                Text(title).font(.caption)
            }
            .frame(maxWidth: .infinity, minHeight: 44)
        }
        .buttonStyle(.bordered)
        .accessibilityIdentifier(id)
    }
}
