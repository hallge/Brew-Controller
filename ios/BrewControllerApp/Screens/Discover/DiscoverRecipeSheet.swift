// "Make it a recipe": the recipe from the recipe book or the trending list, filled in to check and save
// (the web page fills the recipe form: "Filled in: check it and save").
import SwiftUI

struct DiscoverRecipeSheet: View {
    @State var draft: DiscoverRecipeDraft
    let onSave: (DiscoverRecipeDraft) async -> Bool

    @Environment(\.dismiss) private var dismiss
    @State private var saving = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("Name", text: $draft.name)
                        .accessibilityIdentifier("discoverRecipeName")
                        .onChange(of: draft.name) { _, text in if text.count > 23 { draft.name = String(text.prefix(23)) } }
                    numberRow("Dose in (g)", value: $draft.doseG, id: "discoverRecipeDose")
                    numberRow("Yield out (g)", value: $draft.yieldG, id: "discoverRecipeYield")
                    numberRow("Target time (s)", value: $draft.timeS, id: "discoverRecipeTime")
                    if draft.holdS > 0 { numberRow("Pre-infusion hold (s)", value: $draft.holdS, id: "discoverRecipeHold") }
                    if draft.bloomS > 0 { numberRow("Bloom pause (s)", value: $draft.bloomS, id: "discoverRecipeBloom") }
                } footer: {
                    Text("Filled in: check it and save. It becomes a new recipe on the Recipes page.")
                }
            }
            .scrollContentBackground(.hidden)
            .background(BrewTheme.shared.palette.bg)
            .navigationTitle("Make it a recipe")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") {
                        Task {
                            saving = true
                            if await onSave(draft) { dismiss() }
                            saving = false
                        }
                    }
                    .disabled(saving || draft.doseG <= 0 || draft.yieldG <= 0)
                    .accessibilityIdentifier("discoverRecipeSave")
                }
            }
        }
    }

    private func numberRow(_ title: String, value: Binding<Double>, id: String) -> some View {
        HStack {
            Text(title)
            Spacer()
            TextField("0", value: value, format: .number.precision(.fractionLength(0...1)))
                .keyboardType(.decimalPad).multilineTextAlignment(.trailing).frame(width: 80)
                .accessibilityIdentifier(id)
        }
    }
}
