// The More tab: a list of the screens that do not have a tab. Each row is a placeholder that its own
// work replaces (put the real view in Screens/<Name>/ and point the row at it).
import SwiftUI

struct MoreTab: View {
    static let rows = ["Recipes", "Machine", "Insights", "Subscriptions", "Discover", "Community", "Devices", "Setup", "Logs"]

    var body: some View {
        NavigationStack {
            List(Self.rows, id: \.self) { name in
                NavigationLink(name) { ComingSoonView(title: name) }
                    .accessibilityIdentifier("moreRow-\(name)")
            }
            .navigationTitle("More")
        }
        .accessibilityIdentifier("moreTab")
    }
}
