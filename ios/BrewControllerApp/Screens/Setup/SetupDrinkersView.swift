// Drinkers (web: Setup › Drinkers): named drinkers on or off, and up to eight names.
import SwiftUI

struct SetupDrinkersView: View {
    var store: SetupStore
    @State private var named = false
    @State private var names: [String] = Array(repeating: "", count: 8)
    @State private var loadedOnce = false

    var body: some View {
        Form {
            SetupMessages(error: store.error, notice: store.notice)
            Section {
                Toggle("Named drinkers", isOn: $named).accessibilityIdentifier("setupNamedDrinkers")
            } footer: {
                Text("Each rating says who gave it; each drinker gets their own taste and recommendations. Off: one household profile.")
            }
            Section {
                ForEach(0..<8, id: \.self) { index in
                    TextField("Drinker \(index + 1)", text: $names[index])
                        .onChange(of: names[index]) { _, value in
                            if value.count > 15 { names[index] = String(value.prefix(15)) }
                        }
                        .accessibilityIdentifier("setupDrinker\(index + 1)")
                }
            } header: {
                Text("Names")
            } footer: {
                Text("Clearing a name keeps that drinker's ratings.")
            }
            Section {
                Button("Save names") { Task { await store.saveDrinkers(named: named, names: names.map { $0.trimmingCharacters(in: .whitespaces) }) } }
                    .accessibilityIdentifier("setupDrinkersSave")
            }
        }
        .navigationTitle("Drinkers")
        .task {
            if !store.loaded { await store.refresh() }
            if !loadedOnce {
                named = store.drinkers.named
                for (index, name) in store.drinkers.drinkers.prefix(8).enumerated() { names[index] = name }
                loadedOnce = true
            }
        }
    }
}
