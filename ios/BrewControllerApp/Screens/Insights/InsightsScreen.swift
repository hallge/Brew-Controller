// Insights (web page: #insights). Pushed by the More tab inside its NavigationStack: no stack of its own.
import SwiftUI

struct InsightsScreen: View {
    @State private var store = InsightsStore()

    var body: some View {
        let palette = BrewTheme.shared.palette
        ScrollView {
            VStack(spacing: 14) {
                switch store.phase {
                case .loading:
                    ProgressView("Loading insights")
                        .frame(maxWidth: .infinity, minHeight: 160)
                        .accessibilityIdentifier("insightsLoading")
                case .failed(let message):
                    InsightsCard(title: "Insights", note: "", identifier: "insightsFailed") {
                        Text(message).font(.subheadline).foregroundStyle(palette.dim)
                        Button("Try again") { Task { await store.load() } }
                            .buttonStyle(.borderedProminent)
                            .accessibilityIdentifier("insightsRetry")
                    }
                case .loaded:
                    content
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
        }
        .background(palette.bg)
        .navigationTitle("Insights")
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                if store.coffeeChoices.count > 0 {
                    Menu {
                        Picker("Coffee", selection: coffeeSelection) {
                            Text("All coffees").tag(-1)
                            ForEach(store.coffeeChoices, id: \.id) { choice in
                                Text(choice.name).tag(choice.id)
                            }
                        }
                    } label: {
                        Label(store.coffeeFilterName, systemImage: "line.3.horizontal.decrease.circle")
                    }
                    .accessibilityIdentifier("insightsCoffeeFilter")
                }
            }
        }
        .refreshable { await store.load() }
        .task { await store.load() }
        .accessibilityIdentifier("insightsScreen")
    }

    /// -1 is "All coffees".
    private var coffeeSelection: Binding<Int> {
        Binding(get: { store.coffeeFilter ?? -1 }, set: { store.coffeeFilter = $0 == -1 ? nil : $0 })
    }

    @ViewBuilder
    private var content: some View {
        let shots = store.filteredShots
        let grinder = store.library.grinder
        InsightsTasteCard(profile: store.profile, ratings: store.ratings, drinker: store.drinker,
                          coffeeName: { store.coffeeName($0) },
                          onChoose: { id in Task { await store.chooseDrinker(id) } })
        InsightsRoastGrindCard(shots: shots, calibration: grinder.calibration, dial: grinder.dial,
                               tolerance: store.tolerance)
        InsightsAgeCard(shots: shots, tolerance: store.tolerance)
        InsightsCalendarCard(shots: shots, powerEvents: store.powerEvents, tolerance: store.tolerance,
                             currentCoffeeId: store.currentCoffeeId)
        InsightsDialInCard(shots: shots, tolerance: store.tolerance)
        InsightsGrindCard(accuracy: store.accuracy, dial: grinder.dial, coffeeName: { store.coffeeName($0) })
    }
}
