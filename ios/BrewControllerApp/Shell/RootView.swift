// The app's root: the first-run screens until onboarding is complete, then the five tabs (Brew, Shots,
// Coffees, Grinder, More). The controller starts once the first run says which devices to use. Also the
// dim overlay (the board's dimming) and the "Restored, tap to restart" screen.
import SwiftUI

struct RootView: View {
    @State private var onboarding = Onboarding.shared
    @State private var theme = BrewTheme.shared
    @State private var lifecycle = AppLifecycle.shared
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.colorScheme) private var colorScheme

    /// The controller can run: the UI tests skip the first run, otherwise it starts when the device choice
    /// (and, for real devices, the Bluetooth step) is behind us.
    private var controllerWanted: Bool {
        if Controller.skipsOnboarding { return true }
        switch onboarding.step {
        case .diagnostics, .done: return true
        default: return false
        }
    }

    private var showsTabs: Bool { Controller.skipsOnboarding || onboarding.isComplete }

    var body: some View {
        ZStack {
            theme.palette.bg.ignoresSafeArea()
            if showsTabs {
                BrewMainTabs()
            } else {
                BrewOnboardingFlow()
            }
            if lifecycle.restartPending {
                BrewRestartView()
            }
            if lifecycle.isDimmed {
                // The board dims an idle screen; any touch brings it back (`brewTracksActivity`).
                Color.black.opacity(0.75).ignoresSafeArea().allowsHitTesting(false)
                    .accessibilityIdentifier("dimOverlay")
            }
        }
        .preferredColorScheme(theme.preferredColorScheme)
        .onChange(of: colorScheme, initial: true) { _, new in theme.updateSystem(colorScheme: new) }
        .onChange(of: scenePhase) { _, phase in lifecycle.scenePhaseChanged(phase) }
        .onChange(of: controllerWanted, initial: true) { _, wanted in
            if wanted { Controller.startOnce() }
        }
        .brewTracksActivity(lifecycle)
    }
}

/// Brew · Shots · Coffees · Grinder · More, each root in its own NavigationStack. Only Brew is built;
/// the others are placeholders that later work replaces, each in its own folder (Screens/<Name>/).
struct BrewMainTabs: View {
    @State private var theme = BrewTheme.shared

    var body: some View {
        TabView {
            NavigationStack { BrewTab().toolbar(.hidden, for: .navigationBar) }
                .tabItem { Label("Brew", systemImage: "cup.and.saucer.fill") }
            NavigationStack { ShotsTab() }
                .tabItem { Label("Shots", systemImage: "list.bullet.rectangle") }
            NavigationStack { CoffeesTab() }
                .tabItem { Label("Coffees", systemImage: "leaf.fill") }
            NavigationStack { GrinderTab() }
                .tabItem { Label("Grinder", systemImage: "gearshape.2.fill") }
            NavigationStack { BrewMoreTab() }
                .tabItem { Label("More", systemImage: "ellipsis.circle") }
        }
        .tint(theme.palette.accent)
        .accessibilityIdentifier("mainTabs")
    }
}

/// "Restored, tap to restart": the firmware cannot run its set-up twice, so the app ends itself and a
/// notification offers the way back (AppLifecycle.confirmRestart).
struct BrewRestartView: View {
    @State private var lifecycle = AppLifecycle.shared

    var body: some View {
        ZStack {
            brewPal.bg.ignoresSafeArea()
            Button {
                Task { await lifecycle.confirmRestart() }
            } label: {
                Text(lifecycle.restartMessage)
                    .font(.title2.weight(.semibold))
                    .padding(24)
                    .frame(maxWidth: .infinity)
                    .background(brewPal.accent, in: RoundedRectangle(cornerRadius: 16))
                    .foregroundStyle(brewPal.bg)
                    .padding(24)
            }
            .accessibilityIdentifier("restartButton")
        }
        .accessibilityIdentifier("restartScreen")
    }
}
