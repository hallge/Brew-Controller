// One recipe: dose and yield, pre-infusion and bloom, what happens if the target is never reached, save,
// duplicate, delete and share, and which coffees it is used with (WebPage.h #recipes).
import SwiftUI

struct RecipeFormView: View {
    let store: RecipeStore
    /// 0: a new recipe.
    let recipeId: Int

    @Environment(\.dismiss) private var dismiss
    @State private var draft = RecipeDraft()
    @State private var loadedId: Int?
    @State private var confirmDelete = false
    @State private var saving = false

    private var recipe: RecipeDefModel? { store.recipes.first { $0.id == recipeId } }
    private var isNew: Bool { recipeId == 0 }

    var body: some View {
        Form {
            Section {
                TextField("Name", text: $draft.name)
                    .font(.headline)
                    .onChange(of: draft.name) { _, text in
                        if text.count > 23 { draft.name = String(text.prefix(23)) }
                    }
                    .accessibilityIdentifier("recipeName")
            }

            Section {
                RecipeNumberRow(title: "Dose in (g)", value: $draft.doseG, identifier: "recipeDose")
                Picker("Yield set by", selection: $draft.useRatio) {
                    Text("Target weight").tag(false)
                    Text("Ratio").tag(true)
                }
                .pickerStyle(.segmented)
                .accessibilityIdentifier("recipeYieldMode")
                if draft.useRatio {
                    RecipeNumberRow(title: "Ratio 1:", value: $draft.ratio, identifier: "recipeRatio")
                    HStack {
                        Text("Target out (g)")
                        Spacer()
                        Text(BrewFormat.grams(draft.effectiveTargetG)).foregroundStyle(.secondary)
                    }
                } else {
                    RecipeNumberRow(title: "Target out (g)", value: $draft.targetWeightG, identifier: "recipeTarget")
                }
                RecipeNumberRow(title: "Target brew time (s)", value: $draft.targetTimeS, identifier: "recipeTime")
            } header: {
                Text("Dose and yield")
            } footer: {
                Text("The target brew time is what grind advice steers toward.")
            }

            Section {
                Picker("How the button is pressed", selection: preinfusionBinding) {
                    Text("Tap: the machine's own pre-infusion").tag(0)
                    Text("Hold the button for a set time").tag(1)
                }
                .disabled(draft.bloomPauseS > 0)
                .accessibilityIdentifier("recipePreinfusion")
                if draft.effectivePreinfusion == 1 {
                    RecipeNumberRow(title: "Hold time (s)", value: $draft.preinfusionHoldS, identifier: "recipeHold")
                }
                RecipeNumberRow(title: "Bloom pause (s, 0 = none)", value: $draft.bloomPauseS, identifier: "recipeBloom")
            } header: {
                Text("Pre-infusion and bloom")
            } footer: {
                Text(bloomNote)
            }

            Section {
                Picker("Who ends the shot", selection: $draft.timeLimit) {
                    Text("The machine's own time-out").tag(0)
                    Text("The Bot, after a set time").tag(1)
                }
                .accessibilityIdentifier("recipeTimeLimit")
                if draft.timeLimit == 1 {
                    RecipeNumberRow(title: "Stop after (s)", value: $draft.maxShotTimeS, identifier: "recipeMaxTime")
                }
                RecipeNumberRow(title: "Starting stop offset, until learned (g)", value: $draft.baselineOffsetG,
                                identifier: "recipeOffset")
            } header: {
                Text("If the target is never reached")
            } footer: {
                Text("A new coffee on this recipe starts from what the recipe has learned on its other coffees; this is used until it has learned one.")
            }

            Section {
                Button("Save") { Task { await save() } }
                    .disabled(saving)
                    .accessibilityIdentifier("recipeSave")
                if !isNew {
                    shareControls
                }
            } footer: {
                if isNew { Text("New recipe: not saved yet.") }
            }

            if !isNew {
                usedWith
                Section {
                    Button("Duplicate") { Task { await duplicate() } }
                        .accessibilityIdentifier("recipeDuplicate")
                    Button("Delete", role: .destructive) { confirmDelete = true }
                        .accessibilityIdentifier("recipeDelete")
                }
            }
        }
        .accessibilityIdentifier("recipeForm")
        .recipeThemed()
        .navigationTitle(isNew ? "New recipe" : (recipe?.name ?? "Recipe"))
        .navigationBarTitleDisplayMode(.inline)
        .onAppear(perform: loadDraft)
        .onChange(of: store.library) { _, _ in loadDraft() }
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("Done") { Task { await endEditing() } }
            }
        }
        .confirmationDialog("Delete the recipe \"\(recipe?.name ?? "")\"?", isPresented: $confirmDelete,
                            titleVisibility: .visible) {
            Button("Delete", role: .destructive) { Task { await delete() } }
        }
        .recipeNotices(store)
    }

    // MARK: Pieces

    /// Reading the pre-infusion shows "hold" while a bloom is set: a bloom forces a held pre-infusion.
    private var preinfusionBinding: Binding<Int> {
        Binding(get: { draft.effectivePreinfusion }, set: { draft.preinfusion = $0 })
    }

    private var bloomNote: String {
        draft.bloomPauseS > 0
            ? "Bloom: the button is held for the hold time, then pressed again to switch the pump off. After the pause it is held for one second, which skips the machine's own pre-infusion, and the brew runs to the target. The pause counts toward the brew time, and \"Stop after\" must leave room for it."
            : "Holding MANUAL on the Breville keeps it in low-pressure pre-infusion for as long as it is held. A bloom pause switches the pump off after the hold and starts it again after the pause; it needs a held pre-infusion."
    }

    @ViewBuilder
    private var shareControls: some View {
        let entry = store.pending?.entry(for: recipeId)
        let shared = entry.map { $0.state != "local" } ?? false
        Button(shared ? "Share again" : "Share to the recipe book") {
            Task { await store.share(recipe: recipeId) }
        }
        .disabled(!(store.pending?.linked ?? false))
        .accessibilityIdentifier("recipeShare")
        if !(store.pending?.linked ?? false) {
            Text("Link the shared library in Setup first.").font(.footnote).foregroundStyle(.secondary)
        }
        if let entry, shared {
            Text("Recipe book: \(RecipeShareText.state(entry.state))" + (entry.note.isEmpty ? "" : ": \(entry.note)"))
                .font(.footnote)
                .foregroundStyle(.secondary)
                .accessibilityIdentifier("recipeShareState")
        }
    }

    @ViewBuilder
    private var usedWith: some View {
        let library = store.library
        let entries = (library?.presets ?? []).enumerated().filter { $0.element.recipeId == recipeId }
        let dial = library?.grinder.dial ?? .microns
        Section {
            if entries.isEmpty {
                Text("Not on the controller yet.").foregroundStyle(.secondary)
            }
            ForEach(entries, id: \.offset) { index, preset in
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text(library?.coffee(id: preset.coffeeId)?.name ?? "No coffee attached").font(.headline)
                        if index == library?.activePreset {
                            Text("in use now").font(.caption).foregroundStyle(.secondary)
                        }
                        if preset.hidden {
                            Text("bag finished").font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    Text(usedLine(preset, dial: dial))
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    HStack {
                        Button("Reset offset") { Task { await store.resetOffset(entry: index) } }
                            .accessibilityIdentifier("recipeResetOffset-\(index)")
                        Spacer()
                        Button("Take off the controller") { Task { await store.takeOff(entry: index) } }
                            .accessibilityIdentifier("recipeTakeOff-\(index)")
                    }
                    .buttonStyle(.borderless)
                    .font(.subheadline)
                }
                .accessibilityElement(children: .contain)
                .accessibilityIdentifier("recipeUsedWith-\(index)")
            }
            if !entries.contains(where: { $0.element.coffeeId == 0 }) {
                Button("List it on the controller by itself") { Task { await store.listAlone(recipe: recipeId) } }
                    .accessibilityIdentifier("recipeListAlone")
            }
            NavigationLink {
                CoffeesTab()
            } label: {
                Text("Add to a coffee")
            }
            .accessibilityIdentifier("recipeAddToCoffee")
        } header: {
            Text("Used with")
        } footer: {
            Text("The controller lists coffee and recipe as one line to tap. A recipe listed by itself is for guests and one-off bags.")
        }
    }

    private func usedLine(_ preset: PresetModel, dial: BrewFormat.Dial) -> String {
        let grind = preset.grindUm > 0 ? dial.settingUnit(preset.grindUm) : "not set"
        return "Grind " + grind + " · Learned stop offset " + BrewFormat.decimal(preset.stopOffsetG, 2) + " g"
    }

    // MARK: Actions

    /// Fills the form from the controller's recipe, once for each recipe shown.
    private func loadDraft() {
        guard loadedId != recipeId else { return }
        if isNew {
            loadedId = 0
            return
        }
        guard let recipe else { return }
        draft = RecipeDraft(recipe)
        loadedId = recipeId
    }

    /// Ends the edit in the number field that has the keyboard, so its last digits are in `draft`.
    private func endEditing() async {
        UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
        try? await Task.sleep(for: .milliseconds(60))
    }

    private func save() async {
        saving = true
        defer { saving = false }
        await endEditing()
        guard await store.save(draft, id: recipeId) != nil else { return }
        if isNew { dismiss() } else { draft = draft.clamped() }
    }

    private func duplicate() async {
        await endEditing()
        if await store.duplicate(draft) != nil { dismiss() }
    }

    private func delete() async {
        if await store.delete(recipe: recipeId) { dismiss() }
    }
}

/// A labelled number field. It keeps its own text and writes the number into the form on every keystroke,
/// so Save never depends on the field having ended its edit (a number pad has no Return key). The value is
/// clamped when the recipe is saved.
private struct RecipeNumberRow: View {
    let title: String
    @Binding var value: Double
    let identifier: String

    @State private var text = ""

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            TextField("", text: $text)
                .keyboardType(.decimalPad)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 90)
                .accessibilityIdentifier(identifier)
        }
        .onAppear { text = Self.format(value) }
        .onChange(of: text) { _, newText in
            if let parsed = Double(newText.replacingOccurrences(of: ",", with: ".")), parsed != value { value = parsed }
        }
        .onChange(of: value) { _, newValue in
            let typed = Double(text.replacingOccurrences(of: ",", with: "."))
            if typed != newValue { text = Self.format(newValue) }
        }
    }

    /// "18", "1.5", "0.3": no trailing zeros.
    private static func format(_ number: Double) -> String {
        var text = BrewFormat.decimal(number, 2)
        while text.contains(".") && (text.hasSuffix("0") || text.hasSuffix(".")) { text.removeLast() }
        return text
    }
}
