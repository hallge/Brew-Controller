// The Setup screen (web: #setup; board: Settings). Pushed by More inside its NavigationStack, so no
// NavigationStack of its own; its sub-screens are pushed with NavigationLink.
import SwiftUI

struct SetupScreen: View {
    @State private var store = SetupStore()
    @State private var confirmMode = false
    @State private var confirmFirstRun = false

    private var theme: BrewTheme { BrewTheme.shared }
    private var isDemo: Bool { LiveModel.shared.live.demo }

    var body: some View {
        List {
            SetupMessages(error: store.error, notice: store.notice)
            appearanceSection
            Section("Setup") {
                link("Brew", "cup.and.saucer", id: "setupBrewLink") { SetupBrewView(store: store) }
                link("Drinkers", "person.2", id: "setupDrinkersLink") { SetupDrinkersView(store: store) }
                link("Home Assistant", "house", id: "setupHaLink") { SetupHomeAssistantView(store: store) }
                link("Backup and restore", "externaldrive", id: "setupBackupLink") { SetupBackupView(store: store) }
                link("Shared library", "person.3", id: "setupShareLink") { SetupSharingView(store: store) }
                link("Diagnostics", "stethoscope", id: "setupDiagLink") { SetupDiagnosticsView(store: store) }
                link("Getting started", "list.number", id: "setupStartedLink") { SetupStartedScreen() }
                if store.addonCount > 0 {
                    link("Add-ons", "puzzlepiece.extension", id: "setupAddonsLink") { SetupAddonsView() }
                }
            }
            panelsSection
            appSection
        }
        .navigationTitle("Setup")
        .accessibilityIdentifier("setupScreen")
        .task { await store.refresh() }
        .refreshable { await store.refresh() }
        .confirmationDialog("Show the first run again?", isPresented: $confirmFirstRun, titleVisibility: .visible) {
            Button("Show it again") { Onboarding.shared.reset() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The safety notice and the choice of demo or real devices come back now. Nothing on the controller changes.")
        }
        .confirmationDialog(isDemo ? "Use real devices?" : "Use the demo?", isPresented: $confirmMode, titleVisibility: .visible) {
            Button("Switch and restart") { Task { await switchMode(toReal: isDemo) } }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The app restarts to switch. The demo and the real devices keep separate data.")
        }
    }

    private func link<Destination: View>(_ title: String, _ symbol: String, id: String,
                                         @ViewBuilder destination: () -> Destination) -> some View {
        NavigationLink {
            destination()
        } label: {
            Label(title, systemImage: symbol)
        }
        .accessibilityIdentifier(id)
    }

    // MARK: Appearance

    private var appearanceSection: some View {
        Section {
            ForEach(BrewThemeID.allCases) { item in
                Button {
                    Task { await store.chooseTheme(item) }
                } label: {
                    HStack {
                        SetupSwatch(theme: item)
                        Text(item.name).foregroundStyle(.primary)
                        Spacer()
                        if !theme.followsSystem && theme.controllerTheme == item {
                            Image(systemName: "checkmark").foregroundStyle(Color.accentColor)
                        }
                    }
                }
                .accessibilityIdentifier("setupTheme_\(item.rawValue)")
                .accessibilityValue(!theme.followsSystem && theme.controllerTheme == item ? "selected" : "")
            }
            Toggle("iPhone appearance", isOn: Binding(
                get: { theme.followsSystem },
                set: { theme.setFollowsSystem($0) }))
                .accessibilityIdentifier("setupFollowSystem")
        } header: {
            Text("Appearance")
        } footer: {
            Text("The colour scheme of this app and of the controller's own screen. iPhone appearance follows the system's light and dark instead.")
        }
    }

    // MARK: Show in More

    private var panelsSection: some View {
        Section {
            ForEach(SetupStore.panels) { panel in
                Toggle(isOn: Binding(get: { store.panelShown(panel) },
                                     set: { value in Task { await store.setPanel(panel, shown: value) } })) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(panel.title)
                        Text(panel.detail).font(.caption).foregroundStyle(.secondary)
                    }
                }
                .accessibilityIdentifier("setupPanel_\(panel.id)")
            }
        } header: {
            Text("Show in More")
        } footer: {
            Text("Extra pages and cards. Saved on the controller.")
        }
    }

    // MARK: This app

    private var appSection: some View {
        Section {
            HStack {
                Text("Devices")
                Spacer()
                Text(isDemo ? "Demo (simulated)" : "Real devices").foregroundStyle(.secondary)
                    .accessibilityIdentifier("setupModeState")
            }
            Button(isDemo ? "Use real devices" : "Use the demo") { confirmMode = true }
                .accessibilityIdentifier("setupModeSwitch")
            NavigationLink("Safety notice") { SetupSafetyView() }
                .accessibilityIdentifier("setupSafetyLink")
            Button("Show first run again") { confirmFirstRun = true }
                .accessibilityIdentifier("setupFirstRun")
            versionRow("App", appVersion, id: "setupAppVersion")
            if !store.firmware.isEmpty {
                versionRow("Controller firmware", store.firmware + (store.built.isEmpty ? "" : " (\(store.built))"), id: "setupFirmwareVersion")
            }
        } header: {
            Text("This app")
        } footer: {
            Text("The demo runs a simulated scale, bot and machine and keeps its own data. Switching restarts the app: it closes, and you open it again.")
        }
    }

    private var appVersion: String {
        let info = Bundle.main.infoDictionary
        let short = info?["CFBundleShortVersionString"] as? String ?? ""
        let build = info?["CFBundleVersion"] as? String ?? ""
        return build.isEmpty ? short : "\(short) (\(build))"
    }

    private func versionRow(_ title: String, _ value: String, id: String) -> some View {
        HStack {
            Text(title)
            Spacer()
            Text(value).foregroundStyle(.secondary).accessibilityIdentifier(id)
        }
    }

    private func switchMode(toReal: Bool) async {
        _ = Onboarding.shared.switchMode(toReal ? .real : .demo)
        await AppLifecycle.shared.confirmRestart()
    }
}

/// The three main colours of a scheme, as a small swatch.
struct SetupSwatch: View {
    var theme: BrewThemeID

    var body: some View {
        let palette = BrewPalette.load(theme: theme.rawValue)
        HStack(spacing: 0) {
            palette.bg
            palette.accent
            palette.ok
        }
        .frame(width: 42, height: 22)
        .clipShape(RoundedRectangle(cornerRadius: 5))
        .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.secondary.opacity(0.5), lineWidth: 0.5))
    }
}

/// The safety notice, to read again (the wording of the first-run notice, docs/SAFETY.md condensed).
struct SetupSafetyView: View {
    var body: some View {
        List {
            Text("Brew Controller is a hobby project, not a certified appliance. It presses a button on your espresso machine to stop a shot at a target weight. It has not been tested by any safety body and comes without warranty.")
            Text("It is an aid to stopping at a weight, not a safety device. Be present while a shot runs: if the scale or the button pusher is lost, a machine that was started keeps running until you stop it by hand.")
            Text("Check the first shots by hand before leaving it to itself.")
        }
        .navigationTitle("Before you brew")
        .accessibilityIdentifier("setupSafetyScreen")
    }
}
