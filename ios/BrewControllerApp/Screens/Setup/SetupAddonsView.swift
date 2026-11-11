// Add-ons (web: Add-ons card; docs/ADDONS.md): the add-ons the firmware has, each switched on or off, with its
// settings, the recipe map of a grinder add-on and "Forget its settings". Shown from Setup only when the
// controller lists any. Not drawn here: an add-on's own steering/comparison card, charts and module delivery.
import SwiftUI

@MainActor
@Observable
final class SetupAddonsStore {
    var addons: [SetupAddon] = []
    var recipes: [RecipeDefModel] = []
    var loaded = false
    var error: String?
    var notice: String?

    @ObservationIgnored private let api: BrewAPI

    init(api: BrewAPI = .shared) {
        self.api = api
    }

    func refresh() async {
        if let list = try? await api.get("/api/addons", as: SetupAddonList.self) {
            addons = list.addons
            loaded = true
        }
        if let library = try? await api.get("/api/library", as: LibraryModel.self) { recipes = library.recipes }
    }

    private func perform(_ done: String?, _ work: () async throws -> Void) async {
        error = nil
        notice = nil
        do {
            try await work()
            if let done { notice = done }
        } catch let failure as BrewAPIError {
            error = failure.isConflict ? "Not while a shot is running." : failure.message
        } catch {
            self.error = String(describing: error)
        }
        await refresh()
    }

    func enable(_ id: String, _ on: Bool) async {
        await perform(on ? "Switched on" : "Switched off") {
            try await api.post("/api/addons/enable", query: ["id": id, "on": on ? "1" : "0"])
        }
    }

    func save(_ id: String, values: [String: String], options: SetupAddonOptions) async {
        await perform("Saved") {
            try await api.post("/api/addons/config", json: [
                "id": .string(id),
                "values": .object(values.mapValues { BrewJSON.string($0) }),
                "options": .object(["dose_sets_target": .bool(options.doseSetsTarget),
                                    "select_recipe": .bool(options.selectRecipe),
                                    "shot_feedback": .bool(options.shotFeedback)]),
            ])
        }
    }

    func map(_ id: String, grinderRecipe: Int, recipe: Int) async {
        await perform("Saved") {
            try await api.post("/api/addons/map", query: ["id": id, "grinder": String(grinderRecipe), "recipe": String(recipe)])
        }
    }

    func readNow(_ id: String) async {
        error = nil
        notice = nil
        do {
            let response = try await api.post("/api/addons/do", query: ["id": id, "cmd": "read"])
            let object = try? JSONSerialization.jsonObject(with: response.body) as? [String: Any]
            let reply = object?["reply"] as? String ?? ""
            notice = reply.isEmpty ? "Done" : reply
        } catch let failure as BrewAPIError {
            error = failure.message
        } catch {
            self.error = String(describing: error)
        }
        try? await Task.sleep(for: .seconds(4))
        await refresh()
    }

    func forget(_ id: String) async {
        await perform("Forgotten") {
            try await api.post("/api/addons/forget", query: ["id": id])
        }
    }
}

struct SetupAddonsView: View {
    @State private var store = SetupAddonsStore()

    var body: some View {
        List {
            SetupMessages(error: store.error, notice: store.notice)
            if store.addons.isEmpty && store.loaded {
                Text("This controller has no add-ons.").foregroundStyle(.secondary)
            }
            ForEach(store.addons) { addon in
                SetupAddonCard(store: store, addon: addon)
            }
        }
        .navigationTitle("Add-ons")
        .accessibilityIdentifier("setupAddonsScreen")
        .task { await store.refresh() }
        .refreshable { await store.refresh() }
    }
}

private struct SetupAddonCard: View {
    var store: SetupAddonsStore
    var addon: SetupAddon
    @State private var values: [String: String] = [:]
    @State private var options = SetupAddonOptions()
    @State private var confirmForget = false

    var body: some View {
        Section {
            Toggle("On", isOn: Binding(get: { addon.enabled }, set: { value in Task { await store.enable(addon.id, value) } }))
                .disabled(!addon.apiOk)
                .accessibilityIdentifier("setupAddonOn_\(addon.id)")
            if !addon.apiOk {
                Text("Built for another add-on interface: it cannot be switched on.").font(.footnote)
            }
            if addon.enabled || !addon.status.isEmpty {
                Text((addon.enabled && !addon.link.isEmpty ? addon.link + ". " : "") + addon.status)
                    .font(.footnote).foregroundStyle(.secondary)
            }
            if let last = addon.lastGrind {
                Text(lastText(last)).font(.footnote).foregroundStyle(.secondary)
            }
            ForEach(addon.fields) { field in
                fieldRow(field)
            }
            if addon.kind == "grinder" {
                Toggle("The measured dose sets the next shot's own target (the recipe keeps its own)", isOn: $options.doseSetsTarget)
                Toggle("A grind selects its recipe here", isOn: $options.selectRecipe)
                if addon.shotFeedbackPossible {
                    Toggle("Report each shot back to the grinder", isOn: $options.shotFeedback)
                }
            }
            Button("Save") {
                Task { await store.save(addon.id, values: values, options: options) }
            }
            .accessibilityIdentifier("setupAddonSave_\(addon.id)")
            if addon.enabled {
                Button("Read the grinder now") { Task { await store.readNow(addon.id) } }
            }
            Button("Forget its settings", role: .destructive) { confirmForget = true }
                .accessibilityIdentifier("setupAddonForget_\(addon.id)")
            if addon.kind == "grinder" { mapRows }
        } header: {
            Text(addon.name.isEmpty ? addon.id : addon.name + (addon.version.isEmpty ? "" : "  " + addon.version))
        }
        .onAppear { load() }
        .onChange(of: addon) { _, _ in load() }
        .confirmationDialog("Forget everything set for this add-on (its settings, the recipe map) and switch it off?",
                            isPresented: $confirmForget, titleVisibility: .visible) {
            Button("Forget", role: .destructive) { Task { await store.forget(addon.id) } }
            Button("Cancel", role: .cancel) {}
        }
    }

    @ViewBuilder
    private func fieldRow(_ field: SetupAddonField) -> some View {
        if field.type == "bool" {
            Toggle(field.label, isOn: Binding(get: { values[field.key] == "1" },
                                              set: { values[field.key] = $0 ? "1" : "0" }))
        } else if field.type == "secret" {
            VStack(alignment: .leading, spacing: 2) {
                SecureField(field.label + (field.set ? " (set; blank keeps it, - forgets it)" : " (not set)"),
                            text: Binding(get: { values[field.key] ?? "" }, set: { values[field.key] = $0 }))
                if !field.help.isEmpty { Text(field.help).font(.caption).foregroundStyle(.secondary) }
            }
        } else {
            VStack(alignment: .leading, spacing: 2) {
                TextField(field.label, text: Binding(get: { values[field.key] ?? "" }, set: { values[field.key] = $0 }))
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                if !field.help.isEmpty { Text(field.help).font(.caption).foregroundStyle(.secondary) }
            }
        }
    }

    @ViewBuilder
    private var mapRows: some View {
        ForEach(1...6, id: \.self) { g in
            let entry = addon.map.first { $0.grinderRecipe == g }
            Picker("Grinder recipe \(g)", selection: Binding(get: { entry?.recipeId ?? 0 },
                                                             set: { value in Task { await store.map(addon.id, grinderRecipe: g, recipe: value) } })) {
                Text(entry == nil ? "not mapped" : "(not mapped)").tag(0)
                ForEach(store.recipes) { recipe in
                    Text(recipe.name).tag(recipe.id)
                }
            }
        }
    }

    private func lastText(_ g: SetupAddonLastGrind) -> String {
        var text = "Last grind \(g.ageS / 60) min ago: grinder recipe \(g.grinderRecipe)"
        if let dose = g.doseG { text += ", \(dose.formatted(.number.precision(.fractionLength(0...2)))) g" }
        if let gap = g.gapUm { text += ", \(gap.formatted(.number.precision(.fractionLength(0...1)))) µm" }
        text += ". "
        if g.mapped {
            text += "Recipe " + (store.recipes.first { $0.id == g.recipeId }?.name ?? String(g.recipeId))
            if !g.setting.isEmpty { text += ", setting \(g.setting)" }
            if g.shotTargetG > 0 { text += ", next shot stops at \(g.shotTargetG.formatted(.number.precision(.fractionLength(0...2)))) g" }
        } else {
            text += "Not mapped to a recipe."
        }
        return text + (g.note.isEmpty ? "" : " " + g.note)
    }

    /// Fills the fields from the controller's; secrets start empty (blank keeps the stored one).
    private func load() {
        var next: [String: String] = [:]
        for f in addon.fields { next[f.key] = f.type == "secret" ? (values[f.key] ?? "") : f.value }
        values = next
        options = addon.options
    }
}
