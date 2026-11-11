// The coffee directory (W: #dircard): every coffee published in the shared library, kept on the controller.
// Search, origin, process and roast filters, 20 to a page, and "Add to my coffees". Also the visualizer.coffee
// lookup (W: "Look up a coffee on visualizer.coffee"), which fills the new-coffee form.
import SwiftUI

struct CoffeeDirectoryView: View {
    let store: CoffeeStore

    @State private var query = ""
    @State private var origin = ""
    @State private var process = ""
    @State private var roast = ""
    @State private var page = 0
    @State private var result: CoffeeDirectoryModel?
    @State private var facets: CoffeeDirectoryFacets?
    @State private var failed = false
    @State private var loading = false

    /// Everything the answer depends on; a change restarts the read (after a short wait while typing).
    private struct Key: Equatable {
        var query: String
        var origin: String
        var process: String
        var roast: String
        var page: Int
    }

    private var key: Key { Key(query: query, origin: origin, process: process, roast: roast, page: page) }

    var body: some View {
        List {
            Section {
                filterPicker("Origin", "Any origin", $origin, facets?.origin)
                filterPicker("Process", "Any process", $process, facets?.process)
                filterPicker("Roast", "Any roast", $roast, facets?.roastLevel)
            } footer: {
                Text("Every coffee published in the shared library, kept on the controller, so it works without the internet too.")
            }

            Section {
                results
            } header: {
                if let count = result?.count, count > 0 { Text("\(count) coffees") }
            }

            pager
        }
        .accessibilityIdentifier("coffeeDirectory")
        .coffeeThemed()
        .navigationTitle("Coffee directory")
        .navigationBarTitleDisplayMode(.inline)
        .searchable(text: $query, prompt: "name, roaster, origin, notes")
        .task(id: key) { await load() }
        .coffeeNotices(store)
    }

    // MARK: Pieces

    private func filterPicker(_ title: String, _ any: String, _ selection: Binding<String>,
                              _ options: [String: Int]?) -> some View {
        Picker(title, selection: selection) {
            Text(any).tag("")
            ForEach((options ?? [:]).keys.sorted(), id: \.self) { name in
                Text("\(name) (\(options?[name] ?? 0))").tag(name)
            }
        }
        .accessibilityIdentifier("coffeeDirectoryFilter-" + title)
        .onChange(of: selection.wrappedValue) { _, _ in page = 0 }
    }

    @ViewBuilder
    private var results: some View {
        if let result {
            if result.keptPages == 0 {
                Text("The directory has not been read from the shared library yet. It comes with the library's next read (within the hour, while the controller is idle).")
                    .foregroundStyle(.secondary)
                    .accessibilityIdentifier("coffeeDirectoryEmpty")
            } else if result.coffees.isEmpty {
                Text("No coffee in the directory matches that.")
                    .foregroundStyle(.secondary)
                    .accessibilityIdentifier("coffeeDirectoryEmpty")
            } else {
                ForEach(result.coffees) { entry in
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(entry.name).font(.headline)
                            Text(detail(entry)).font(.subheadline).foregroundStyle(.secondary)
                            if !entry.notes.isEmpty { Text(entry.notes).font(.caption).foregroundStyle(.secondary) }
                        }
                        Spacer()
                        if entry.onShelf {
                            Text("on your shelf").font(.caption).foregroundStyle(.secondary)
                        } else {
                            Button("Add to my coffees") { Task { await add(entry) } }
                                .buttonStyle(.borderless)
                                .font(.subheadline)
                                .accessibilityIdentifier("coffeeDirectoryAdd-\(entry.id)")
                        }
                    }
                    .accessibilityElement(children: .contain)
                    .accessibilityIdentifier("coffeeDirectoryRow-\(entry.id)")
                }
            }
        } else if failed {
            Text("The directory cannot be read from the controller.").foregroundStyle(.secondary)
        } else {
            HStack { ProgressView(); Text("Loading...").foregroundStyle(.secondary) }
        }
    }

    @ViewBuilder
    private var pager: some View {
        if let result, result.pageCount > 1 {
            Section {
                HStack {
                    Button("Previous") { page = max(0, page - 1) }
                        .disabled(page == 0)
                        .accessibilityIdentifier("coffeeDirectoryPrevious")
                    Spacer()
                    Text("\(result.matched) found · page \(page + 1) of \(result.pageCount)")
                        .font(.footnote).foregroundStyle(.secondary)
                    Spacer()
                    Button("Next") { page += 1 }
                        .disabled(page + 1 >= result.pageCount)
                        .accessibilityIdentifier("coffeeDirectoryNext")
                }
                .buttonStyle(.borderless)
            }
        } else if let result, result.matched > 0 {
            Section { EmptyView() } footer: { Text("\(result.matched) found") }
        }
    }

    private func detail(_ entry: CoffeeLibraryEntry) -> String {
        var text = [entry.roaster, entry.origin, entry.process, entry.roastLevel]
            .filter { !$0.isEmpty }.joined(separator: " · ")
        if entry.stars > 0 {
            text += (text.isEmpty ? "" : " · ") + "★" + BrewFormat.decimal(entry.stars, 1) + " (\(entry.ratings))"
        }
        return text
    }

    // MARK: Loading

    private func load() async {
        // A short wait so typing in the search box does not read the directory for every letter.
        try? await Task.sleep(for: .milliseconds(250))
        if Task.isCancelled { return }
        loading = true
        defer { loading = false }
        do {
            let answer = try await store.directory(query: query.trimmingCharacters(in: .whitespaces), origin: origin,
                                                   process: process, roast: roast, page: page)
            result = answer
            failed = false
            if facets == nil, let found = answer.facets { facets = found }
        } catch {
            if !Task.isCancelled { failed = true }
        }
    }

    private func add(_ entry: CoffeeLibraryEntry) async {
        await store.adopt(entry)
        // The directory marks the coffee as on the shelf once the controller has the new one.
        try? await Task.sleep(for: .milliseconds(800))
        await load()
    }
}

// MARK: - visualizer.coffee

/// "Look up a coffee on visualizer.coffee": the results fill the new-coffee form (the roast date stays
/// empty: it is on the bag). Shown in a sheet with its own NavigationStack.
struct CoffeeVisualizerView: View {
    let store: CoffeeStore
    let onFinished: () -> Void

    @State private var query = ""
    @State private var results: [CoffeeVisualizerBag] = []
    @State private var status = ""
    @State private var searching = false

    var body: some View {
        List {
            Section {
                TextField("coffee or roaster", text: $query)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .submitLabel(.search)
                    .onSubmit { Task { await search() } }
                    .accessibilityIdentifier("coffeeVisualizerQuery")
                Button("Search") { Task { await search() } }
                    .disabled(searching)
                    .accessibilityIdentifier("coffeeVisualizerSearch")
            } footer: {
                Text("Fills the new coffee form with what that shared database knows about the coffee. You add the roast date and your own notes, then Save. Needs a phone that is online. The data is community-contributed and has its own terms.")
            }

            if !status.isEmpty {
                Section { Text(status).foregroundStyle(.secondary).accessibilityIdentifier("coffeeVisualizerStatus") }
            }

            if !results.isEmpty {
                Section {
                    ForEach(results) { bag in
                        NavigationLink {
                            CoffeeDetailView(store: store, coffeeId: 0, draftStart: CoffeeVisualizer.draft(bag),
                                             onFinished: onFinished)
                        } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(bag.name).font(.headline)
                                Text([bag.roaster, bag.country, bag.processing].filter { !$0.isEmpty }.joined(separator: ", "))
                                    .font(.subheadline).foregroundStyle(.secondary)
                            }
                        }
                        .accessibilityIdentifier("coffeeVisualizerResult-\(bag.name)")
                    }
                } header: {
                    Text("Fill the form")
                }
            }
        }
        .coffeeThemed()
        .navigationTitle("visualizer.coffee")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button("Close", action: onFinished).accessibilityIdentifier("coffeeVisualizerClose")
            }
        }
    }

    private func search() async {
        let text = query.trimmingCharacters(in: .whitespaces)
        guard text.count >= 3 else {
            status = "Type at least three letters of the coffee or roaster"
            return
        }
        searching = true
        defer { searching = false }
        status = "Searching..."
        results = []
        do {
            results = try await CoffeeVisualizer.search(text)
            status = results.isEmpty ? "Nothing found." : ""
        } catch let failure as CoffeeVisualizer.Failure {
            status = "Could not reach visualizer.coffee (" + failure.message + "). This needs the phone to be online."
        } catch {
            status = "Could not reach visualizer.coffee (" + error.localizedDescription + "). This needs the phone to be online."
        }
    }
}
