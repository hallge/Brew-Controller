// The More tab root: a list of the screens that have no tab. Each row pushes its screen inside the
// shell's NavigationStack (the screens must not create their own). The rows follow the web page's panel
// switches (Setup, web page panels): Insights, Discover, Subscriptions, Community and Logs show only when
// switched on, and Getting started unless it was hidden. Below: the first run, to show again.
import SwiftUI

struct BrewMoreTab: View {
    private var model: BrewModel { BrewModel.shared }
    @State private var showSafety = false
    @State private var confirmReset = false

    /// Until the settings are read every row shows (a panel is never hidden on a guess).
    private func shown(_ bit: Int, inverted: Bool = false) -> Bool {
        model.settings == nil ? true : model.panelOn(bit, inverted: inverted)
    }

    var body: some View {
        List {
            Section {
                if shown(64, inverted: true) {
                    NavigationLink("Getting started") { SetupStartedScreen() }.accessibilityIdentifier("moreRow-GettingStarted")
                }
                NavigationLink("Recipes") { RecipesScreen() }.accessibilityIdentifier("moreRow-Recipes")
                NavigationLink("Machine") { MachineScreen() }.accessibilityIdentifier("moreRow-Machine")
                if shown(1) {
                    NavigationLink("Insights") { InsightsScreen() }.accessibilityIdentifier("moreRow-Insights")
                }
                if shown(4) {
                    NavigationLink("Subscriptions") { SubscriptionsScreen() }.accessibilityIdentifier("moreRow-Subscriptions")
                }
                if shown(2) {
                    NavigationLink("Discover") { DiscoverScreen() }.accessibilityIdentifier("moreRow-Discover")
                }
                if shown(8) {
                    NavigationLink("Community") { CommunityScreen() }.accessibilityIdentifier("moreRow-Community")
                }
                NavigationLink("Devices") { DevicesScreen() }.accessibilityIdentifier("moreRow-Devices")
                NavigationLink("Setup") { SetupScreen() }.accessibilityIdentifier("moreRow-Setup")
                if shown(16) {
                    NavigationLink("Logs") { LogsScreen() }.accessibilityIdentifier("moreRow-Logs")
                }
            }
            Section("First run") {
                Button("Safety notice") { showSafety = true }
                    .accessibilityIdentifier("moreRow-SafetyNotice")
                Button("Show first run again") { confirmReset = true }
                    .accessibilityIdentifier("moreRow-FirstRun")
            }
        }
        .navigationTitle("More")
        .task { if model.settings == nil { await model.refreshAll() } }
        .sheet(isPresented: $showSafety) { BrewSafetyNoticeSheet() }
        .alert("Show the first run again?", isPresented: $confirmReset) {
            Button("Show it", role: .destructive) { Onboarding.shared.reset() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("The safety notice and the questions come back. Your recipes, coffees and shots are not touched. A different choice of devices takes effect when the app is opened again.")
        }
        .accessibilityIdentifier("moreTab")
    }
}
