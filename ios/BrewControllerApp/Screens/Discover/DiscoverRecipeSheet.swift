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
                    Picker("Yield set by", selection: $draft.useRatio) {
                        Text("Target weight").tag(false)
                        Text("Ratio").tag(true)
                    }
                    .accessibilityIdentifier("discoverRecipeYieldBy")
                    if draft.useRatio {
                        numberRow("Ratio 1:", value: $draft.ratio, id: "discoverRecipeRatio")
                    } else {
                        numberRow("Yield out (g)", value: $draft.yieldG, id: "discoverRecipeYield")
                    }
                    numberRow("Target time (s)", value: $draft.timeS, id: "discoverRecipeTime")
                } header: {
                    Text("Dose and yield")
                } footer: {
                    Text("Filled in: check it and save. It becomes a new recipe on the Recipes page.")
                }
                Section("Pre-infusion and bloom") {
                    Picker("How the button is pressed", selection: holdBinding) {
                        Text("Tap: the machine's own pre-infusion").tag(false)
                        Text("Hold the button for a set time").tag(true)
                    }
                    .accessibilityIdentifier("discoverRecipeHoldMode")
                    if draft.holdS > 0 { numberRow("Hold (s)", value: $draft.holdS, id: "discoverRecipeHold") }
                    numberRow("Bloom pause (s, 0 none)", value: $draft.bloomS, id: "discoverRecipeBloom")
                }
                Section("If the target is never reached") {
                    Picker("Who ends the shot", selection: $draft.timeLimit) {
                        Text("The machine's own time-out").tag(0)
                        Text("The Bot, after a set time").tag(1)
                    }
                    .accessibilityIdentifier("discoverRecipeTimeLimit")
                    if draft.timeLimit == 1 { numberRow("Stop after (s)", value: $draft.maxShotTimeS, id: "discoverRecipeMaxTime") }
                    numberRow("Starting stop offset, until learned (g)", value: $draft.baselineOffsetG,
                              id: "discoverRecipeBaseline")
                }
            }
            .onAppear {
                if draft.ratio <= 0, draft.doseG > 0 { draft.ratio = (draft.yieldG / draft.doseG * 10).rounded() / 10 }
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
                    .disabled(saving || draft.doseG <= 0 || (draft.useRatio ? draft.ratio <= 0 : draft.yieldG <= 0))
                    .accessibilityIdentifier("discoverRecipeSave")
                }
            }
        }
    }

    /// Hold the button (a hold time of 3 s unless one came with the recipe) or tap (no hold).
    private var holdBinding: Binding<Bool> {
        Binding(get: { draft.holdS > 0 }, set: { draft.holdS = $0 ? (draft.holdS > 0 ? draft.holdS : 3) : 0 })
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
