// The More tab root: a list of the screens that have no tab. Each row pushes its screen inside the
// shell's NavigationStack (the screens must not create their own).
import SwiftUI

struct BrewMoreTab: View {
    var body: some View {
        List {
            NavigationLink("Recipes") { RecipesScreen() }.accessibilityIdentifier("moreRow-Recipes")
            NavigationLink("Machine") { MachineScreen() }.accessibilityIdentifier("moreRow-Machine")
            NavigationLink("Insights") { InsightsScreen() }.accessibilityIdentifier("moreRow-Insights")
            NavigationLink("Subscriptions") { SubscriptionsScreen() }.accessibilityIdentifier("moreRow-Subscriptions")
            NavigationLink("Discover") { DiscoverScreen() }.accessibilityIdentifier("moreRow-Discover")
            NavigationLink("Community") { CommunityScreen() }.accessibilityIdentifier("moreRow-Community")
            NavigationLink("Devices") { DevicesScreen() }.accessibilityIdentifier("moreRow-Devices")
            NavigationLink("Setup") { SetupScreen() }.accessibilityIdentifier("moreRow-Setup")
            NavigationLink("Logs") { LogsScreen() }.accessibilityIdentifier("moreRow-Logs")
        }
        .navigationTitle("More")
        .accessibilityIdentifier("moreTab")
    }
}
