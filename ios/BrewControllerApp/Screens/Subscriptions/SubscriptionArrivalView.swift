// "A delivery arrived" (web page: #subarr): pick or make the coffee, the roast date printed on the bag
// and an optional weight. Puts the bag on the shelf unopened and marks the subscription arrived.
import SwiftUI

struct SubscriptionArrivalView: View {
    let store: SubscriptionsStore
    let item: SubscriptionItem

    @Environment(\.dismiss) private var dismiss
    @State private var choice: SubscriptionsStore.ArrivalChoice
    @State private var hasRoast = false
    @State private var roast = Date()
    @State private var weightText = ""
    @State private var newName = ""
    @State private var newRoaster: String
    @State private var newNotes = ""
    @State private var problem = ""
    @State private var saving = false

    init(store: SubscriptionsStore, item: SubscriptionItem) {
        self.store = store
        self.item = item
        let onTheWay = store.onTheWayBags(for: item)
        let shelf = store.shelfCoffees
        var first = SubscriptionsStore.ArrivalChoice.newCoffee
        if let bag = onTheWay.first {
            first = .onTheWay(bag.id)
        } else if let same = shelf.first(where: { !$0.roaster.isEmpty && $0.roaster.lowercased() == item.name.lowercased() }) {
            first = .shelf(same.id)
        }
        _choice = State(initialValue: first)
        _newRoaster = State(initialValue: String((item.site.isEmpty ? item.name : item.site).prefix(23)))
    }

    var body: some View {
        NavigationStack {
            Form {
                Section("Which coffee") {
                    Picker("Which coffee", selection: $choice) {
                        ForEach(store.onTheWayBags(for: item)) { bag in
                            Text("On its way: " + (store.coffeeName(bag.coffeeId) ?? "a bag"))
                                .tag(SubscriptionsStore.ArrivalChoice.onTheWay(bag.id))
                        }
                        Text("A new coffee").tag(SubscriptionsStore.ArrivalChoice.newCoffee)
                        ForEach(store.shelfCoffees) { coffee in
                            Text(coffee.name + (coffee.roaster.isEmpty ? "" : " (\(coffee.roaster))"))
                                .tag(SubscriptionsStore.ArrivalChoice.shelf(coffee.id))
                        }
                    }
                    .pickerStyle(.menu)
                    .accessibilityIdentifier("subscriptionArrivalCoffee")
                }
                if choice == .newCoffee {
                    Section("The new coffee") {
                        TextField("Name", text: $newName)
                            .accessibilityIdentifier("subscriptionArrivalName")
                            .onChange(of: newName) { _, text in if text.count > 23 { newName = String(text.prefix(23)) } }
                        TextField("Roaster", text: $newRoaster)
                            .accessibilityIdentifier("subscriptionArrivalRoaster")
                            .onChange(of: newRoaster) { _, text in if text.count > 23 { newRoaster = String(text.prefix(23)) } }
                        TextField("Tasting notes", text: $newNotes)
                            .accessibilityIdentifier("subscriptionArrivalNotes")
                            .onChange(of: newNotes) { _, text in if text.count > 47 { newNotes = String(text.prefix(47)) } }
                    }
                }
                Section {
                    Toggle("I have the roast date", isOn: $hasRoast)
                        .accessibilityIdentifier("subscriptionArrivalHasRoast")
                    if hasRoast {
                        DatePicker("Roast date printed on the bag", selection: $roast, in: ...Date(), displayedComponents: .date)
                            .accessibilityIdentifier("subscriptionArrivalRoast")
                    }
                    HStack {
                        Text("Weight (g, optional)")
                        Spacer()
                        TextField("0", text: $weightText)
                            .keyboardType(.numberPad).multilineTextAlignment(.trailing).frame(width: 80)
                            .accessibilityIdentifier("subscriptionArrivalWeight")
                    }
                } footer: {
                    Text("Unopened: nothing is tracked until you open it, or it comes up next by roast date when a bag is finished.")
                }
                if !problem.isEmpty {
                    Section { Text(problem).foregroundStyle(BrewTheme.shared.palette.accent) }
                        .accessibilityIdentifier("subscriptionArrivalProblem")
                }
            }
            .scrollContentBackground(.hidden)
            .background(BrewTheme.shared.palette.bg)
            .navigationTitle((item.name.isEmpty ? "Coffee" : item.name) + " arrived")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }.accessibilityIdentifier("subscriptionArrivalCancel")
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Put it on the shelf") { Task { await save() } }
                        .disabled(saving)
                        .accessibilityIdentifier("subscriptionArrivalSave")
                }
            }
        }
        .accessibilityIdentifier("subscriptionArrival")
    }

    private func save() async {
        saving = true
        defer { saving = false }
        let ok = await store.arrive(item, choice: choice, roastDate: hasRoast ? SubscriptionWording.ymd(roast) : 0,
                                    weightG: max(0, Int(weightText) ?? 0), newName: newName,
                                    newRoaster: newRoaster, newNotes: newNotes)
        if ok { dismiss() } else { problem = store.notice?.text ?? "Could not save." }
    }
}
