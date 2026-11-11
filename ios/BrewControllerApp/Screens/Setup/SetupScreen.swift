// The Setup screen (web: #setup; board: Settings). Pushed by More inside its NavigationStack, so no
// NavigationStack of its own; its sub-screens are pushed with NavigationLink.
import SwiftUI

struct SetupScreen: View {
    @State private var store = SetupStore()
    @State private var confirmMode = false

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
                link("Getting started", "list.number", id: "setupStartedLink") { SetupStartedView() }
            }
            panelsSection
            appSection
        }
        .navigationTitle("Setup")
        .accessibilityIdentifier("setupScreen")
        .task { await store.refresh() }
        .refreshable { await store.refresh() }
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
        } header: {
            Text("This app")
        } footer: {
            Text("The demo runs a simulated scale, bot and machine and keeps its own data. Switching restarts the app: it closes, and you open it again.")
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
