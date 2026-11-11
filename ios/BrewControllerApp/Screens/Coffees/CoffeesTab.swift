// Coffees (W: #coffees): the shelf with search, finished bags folded away, the shared library, and the
// ways to add coffees: the form, the directory, visualizer.coffee and a Beanconqueror backup. The shell puts
// this in a NavigationStack, so there is none here (the sheets below have their own).
import SwiftUI
import UniformTypeIdentifiers

struct CoffeesTab: View {
    @State private var store = CoffeeStore()
    @State private var search = ""
    @State private var showNew = false
    @State private var showVisualizer = false
    @State private var showImporter = false
    @State private var includeFinished = false
    @State private var showFinished = false

    var body: some View {
        List {
            shelfSection
            finishedSection
            sharedSection
            elsewhereSection
        }
        .accessibilityIdentifier("coffeesList")
        .coffeeThemed()
        .navigationTitle("Coffees")
        .searchable(text: $search, prompt: "Search by name, roaster, origin or process")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    showNew = true
                } label: {
                    Label("Add a coffee", systemImage: "plus")
                }
                .accessibilityIdentifier("coffeeAdd")
            }
        }
        .task { await store.load() }
        .refreshable { await store.load() }
        .sheet(isPresented: $showNew) {
            NavigationStack {
                CoffeeDetailView(store: store, coffeeId: 0, draftStart: nil, onFinished: { showNew = false })
            }
        }
        .sheet(isPresented: $showVisualizer) {
            NavigationStack {
                CoffeeVisualizerView(store: store, onFinished: { showVisualizer = false })
            }
        }
        .fileImporter(isPresented: $showImporter, allowedContentTypes: [.json, .plainText, .data]) { result in
            Task { await importFile(result) }
        }
        .coffeeNotices(store)
    }

    // MARK: Sections

    private var shelfSection: some View {
        let shelf = store.shelf(matching: search)
        return Section {
            if shelf.isEmpty {
                Text(search.isEmpty ? (store.isLoading ? "Loading..." : "No coffee on the shelf.")
                     : "No coffee on the shelf matches.")
                    .foregroundStyle(.secondary)
                    .accessibilityIdentifier("coffeesEmpty")
            }
            ForEach(shelf) { coffee in
                NavigationLink {
                    CoffeeDetailView(store: store, coffeeId: coffee.id, draftStart: nil, onFinished: nil)
                } label: {
                    CoffeeRow(name: coffee.name, subtitle: store.subtitle(coffee), dim: false)
                }
                .accessibilityIdentifier("coffeeRow-\(coffee.id)")
            }
        } footer: {
            if store.library != nil { Text(store.countLine) }
        }
    }

    @ViewBuilder
    private var finishedSection: some View {
        let finished = store.finished(matching: search)
        if !finished.isEmpty {
            Section {
                DisclosureGroup("Finished bags (\(finished.count))", isExpanded: Binding(
                    get: { showFinished || !search.isEmpty }, set: { showFinished = $0 }
                )) {
                    ForEach(finished) { coffee in
                        NavigationLink {
                            CoffeeDetailView(store: store, coffeeId: coffee.id, draftStart: nil, onFinished: nil)
                        } label: {
                            CoffeeRow(name: coffee.name,
                                      subtitle: [coffee.roaster, "finished " + CoffeeDates.text(coffee.finished)]
                                          .filter { !$0.isEmpty }.joined(separator: " · "),
                                      dim: true)
                        }
                        .accessibilityIdentifier("coffeeRow-\(coffee.id)")
                    }
                }
                .accessibilityIdentifier("coffeesFinished")
            }
        }
    }

    @ViewBuilder
    private var sharedSection: some View {
        let shared = store.sharedMatches(search)
        if !shared.isEmpty {
            Section("In the shared library") {
                ForEach(shared) { entry in
                    HStack {
                        CoffeeRow(name: entry.name, subtitle: entry.summary, dim: false)
                        Spacer()
                        Button("Add") { Task { await store.adopt(entry) } }
                            .buttonStyle(.borderless)
                            .accessibilityIdentifier("coffeeSharedAdd-\(entry.id)")
                    }
                }
            }
        }
    }

    private var elsewhereSection: some View {
        Section {
            NavigationLink {
                CoffeeDirectoryView(store: store)
            } label: {
                Label("Coffee directory", systemImage: "books.vertical")
            }
            .accessibilityIdentifier("coffeeDirectoryLink")
            Button {
                showVisualizer = true
            } label: {
                Label("Look up on visualizer.coffee", systemImage: "magnifyingglass")
            }
            .accessibilityIdentifier("coffeeVisualizer")
            Toggle("Include finished beans", isOn: $includeFinished)
                .accessibilityIdentifier("coffeeBeanconquerorFinished")
            Button {
                showImporter = true
            } label: {
                Label("Import a Beanconqueror backup", systemImage: "square.and.arrow.down")
            }
            .accessibilityIdentifier("coffeeBeanconquerorImport")
        } header: {
            Text("Add coffees from elsewhere")
        } footer: {
            Text("A Beanconqueror backup adds the beans you do not have yet, with their roast dates, opening dates, weights and any time in the freezer. The file stays on your phone. Shots are not imported: Beanconqueror does not know this machine's timing, so they would only confuse the grind advice.")
        }
    }

    // MARK: Import

    private func importFile(_ result: Result<URL, Error>) async {
        switch result {
        case .failure(let error):
            store.errorText = error.localizedDescription
        case .success(let url):
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            guard let data = try? Data(contentsOf: url) else {
                store.errorText = "That file could not be read."
                return
            }
            let message = await store.importBeanconqueror(data, includeFinished: includeFinished)
            store.announce(message)
        }
    }
}

/// A name with its one-line description.
struct CoffeeRow: View {
    let name: String
    let subtitle: String
    let dim: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(name).font(.headline)
            if !subtitle.isEmpty {
                Text(subtitle).font(.subheadline).foregroundStyle(.secondary)
            }
        }
        .opacity(dim ? 0.8 : 1)
    }
}

// MARK: - Look and notices

extension View {
    /// The board's colours behind a list or form.
    @MainActor
    func coffeeThemed() -> some View {
        let palette = BrewTheme.shared.palette
        return self
            .scrollContentBackground(.hidden)
            .background(palette.bg.ignoresSafeArea())
            .tint(palette.accent)
    }

    /// The store's error as an alert and its notice as a short banner at the bottom.
    @MainActor
    func coffeeNotices(_ store: CoffeeStore) -> some View {
        modifier(CoffeeNoticeModifier(store: store))
    }
}

private struct CoffeeNoticeModifier: ViewModifier {
    let store: CoffeeStore

    func body(content: Content) -> some View {
        content
            .overlay(alignment: .bottom) {
                if let notice = store.notice {
                    Text(notice)
                        .font(.subheadline)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 16))
                        .padding(.horizontal, 16)
                        .padding(.bottom, 12)
                        .transition(.opacity)
                        .accessibilityIdentifier("coffeeNotice")
                }
            }
            .animation(.default, value: store.notice)
            .alert("Brew Controller", isPresented: Binding(
                get: { store.errorText != nil },
                set: { if !$0 { store.errorText = nil } }
            )) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(store.errorText ?? "")
            }
    }
}
