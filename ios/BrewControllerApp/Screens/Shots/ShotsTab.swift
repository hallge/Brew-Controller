// Shots (W: #shots): the summary tiles, the filters and the history, the way to a shot's detail and to the
// comparison, the CSV and JSON export, and clearing the history. The shell puts this in a NavigationStack,
// so there is none here.
import SwiftUI

struct ShotsTab: View {
    @State private var store = ShotsStore()
    @State private var shareFile: ShotsSharedFile?
    @State private var confirmClear = false
    @State private var exporting = false

    private let live = LiveModel.shared

    var body: some View {
        List {
            if store.busy {
                Section {
                    Text("A shot is running. The history comes back when it ends.")
                        .foregroundStyle(.secondary)
                        .accessibilityIdentifier("shotsBusy")
                }
            }

            if let latest = store.shots.first {
                Section {
                    NavigationLink {
                        ShotsDetailView(store: store, shotID: latest.id)
                    } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Latest shot").font(.headline)
                            Text(latestLine(latest))
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                        }
                    }
                    .accessibilityIdentifier("shotsLatest")
                }
            }

            Section {
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
                    ForEach(store.summary) { tile in
                        ShotsTile(title: tile.title, value: tile.value, detail: tile.detail)
                            .accessibilityIdentifier("shotsTile-\(tile.title)")
                    }
                }
                .listRowInsets(EdgeInsets(top: 8, leading: 12, bottom: 8, trailing: 12))
                .listRowBackground(Color.clear)
            }

            filters

            if !store.compareIDs.isEmpty {
                Section {
                    NavigationLink {
                        ShotsCompareView(store: store)
                    } label: {
                        Label("Compare shots (\(store.compareIDs.count))", systemImage: "chart.xyaxis.line")
                    }
                    .accessibilityIdentifier("shotsCompareLink")
                }
            }

            history

            Section {
                Button("Clear history", role: .destructive) { confirmClear = true }
                    .accessibilityIdentifier("shotsClear")
            } footer: {
                Text("Open a shot for its curve, its taste, or to leave it out of grind advice. Brew times outside the tolerance are marked; every stored value is in the CSV.")
            }
        }
        .accessibilityIdentifier("shotsList")
        .shotsThemed()
        .navigationTitle("Shots")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button("Download CSV") { Task { await export(csv: true) } }
                        .accessibilityIdentifier("shotsExportCSV")
                    Button("Download JSON") { Task { await export(csv: false) } }
                        .accessibilityIdentifier("shotsExportJSON")
                } label: {
                    Label("Export", systemImage: "square.and.arrow.up")
                }
                .disabled(exporting)
                .accessibilityIdentifier("shotsExport")
            }
        }
        .task { await store.load() }
        .refreshable { await store.load() }
        .onChange(of: store.coffeeFilter) { _, _ in store.shownCount = 20 }
        .onChange(of: store.show) { _, _ in store.shownCount = 20 }
        .onChange(of: live.live.lastShotID) { _, _ in Task { await store.load() } }
        .onChange(of: live.live.isShotInHand) { _, inHand in
            if !inHand { Task { await store.load() } }
        }
        .confirmationDialog("Delete all shot history?", isPresented: $confirmClear, titleVisibility: .visible) {
            Button("Clear history", role: .destructive) { Task { await store.clearHistory() } }
                .accessibilityIdentifier("shotsClearConfirm")
        }
        .shotsShareSheet($shareFile)
        .shotsNotices(store)
    }

    private func latestLine(_ shot: ShotModel) -> String {
        var text = ShotsText.when(shot) + " · " + (shot.coffee.isEmpty ? shot.recipe : shot.coffee)
        text += " · " + BrewFormat.grams(shot.finalWeightG) + " g in " + BrewFormat.seconds(shot.shotTimeS) + " s"
        return text
    }

    // MARK: Filters

    private var filters: some View {
        Section {
            Picker("Coffee", selection: $store.coffeeFilter) {
                Text("All coffees").tag("")
                ForEach(store.coffeeNames, id: \.self) { name in
                    Text(name).tag(name)
                }
            }
            .accessibilityIdentifier("shotsFilterCoffee")
            Picker("Show", selection: $store.show) {
                ForEach(ShotsShow.allCases) { option in
                    Text(option.label).tag(option)
                }
            }
            .accessibilityIdentifier("shotsFilterShow")
        } header: {
            Text("History")
        }
    }

    // MARK: History

    /// The first `shownCount` shots, grouped by day in the order they come.
    private var days: [(day: String, shots: [ShotModel])] {
        var groups: [(day: String, shots: [ShotModel])] = []
        for shot in store.filtered.prefix(store.shownCount) {
            let day = ShotsText.day(shot)
            if let last = groups.indices.last, groups[last].day == day {
                groups[last].shots.append(shot)
            } else {
                groups.append((day: day, shots: [shot]))
            }
        }
        return groups
    }

    @ViewBuilder
    private var history: some View {
        let all = store.filtered
        if all.isEmpty {
            Section {
                Text(store.shots.isEmpty
                     ? (store.loadedOnce ? "No shots yet. Brew one and it appears here." : "Loading...")
                     : "No shots match. Change what is shown above.")
                    .foregroundStyle(.secondary)
                    .accessibilityIdentifier("shotsEmpty")
            }
        }
        ForEach(days, id: \.day) { group in
            Section(group.day) {
                ForEach(group.shots) { shot in
                    NavigationLink {
                        ShotsDetailView(store: store, shotID: shot.id)
                    } label: {
                        ShotsRow(shot: shot, dial: store.dial, tolerance: store.toleranceS)
                    }
                    .accessibilityIdentifier("shotRow-\(shot.id)")
                }
            }
        }
        if all.count > store.shownCount {
            Section {
                Button("Show 20 more") { store.shownCount += 20 }
                    .accessibilityIdentifier("shotsMore")
            } footer: {
                Text("\(store.shownCount) of \(all.count) shown")
            }
        } else if all.count > 20 {
            Section { EmptyView() } footer: { Text("All \(all.count) shown") }
        }
    }

    // MARK: Export

    private func export(csv: Bool) async {
        exporting = true
        defer { exporting = false }
        guard let file = csv ? await store.downloadCSV() : await store.downloadJSON() else { return }
        shareFile = ShotsSharedFile.make(file)
    }
}

/// One row: time, coffee and recipe with rating, taste and end reason, out weight, brew time, grind.
private struct ShotsRow: View {
    let shot: ShotModel
    let dial: BrewFormat.Dial
    let tolerance: Double

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 10) {
            Text(ShotsText.clock(shot))
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 58, alignment: .leading)
            VStack(alignment: .leading, spacing: 2) {
                Text(shot.coffee.isEmpty ? shot.recipe : shot.coffee).font(.headline).lineLimit(1)
                Text(detail).font(.caption).foregroundStyle(.secondary).lineLimit(2)
            }
            Spacer(minLength: 4)
            VStack(alignment: .trailing, spacing: 2) {
                Text(BrewFormat.grams(shot.finalWeightG) + " g").font(.subheadline.monospacedDigit())
                Text(BrewFormat.seconds(shot.shotTimeS) + " s")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(ShotsText.late(shot, tolerance: tolerance) ? Color.red : Color.secondary)
                Text(shot.grindUm > 0 ? dial.setting(shot.grindUm) : "--")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
        }
        .accessibilityElement(children: .combine)
    }

    /// "Double ★★★★ balanced · timeout · ignored"
    private var detail: String {
        var parts: [String] = []
        if !shot.coffee.isEmpty { parts.append(shot.recipe) }
        var line = parts.joined(separator: " ")
        if shot.rating > 0 { line += (line.isEmpty ? "" : " ") + ShotsText.stars(shot.rating) }
        if !shot.taste.isEmpty { line += (line.isEmpty ? "" : " ") + shot.taste }
        if !ShotsText.byWeight(shot) { line += (line.isEmpty ? "" : " · ") + ShotsText.endReason(shot) }
        if shot.grindVote == "ignore" { line += (line.isEmpty ? "" : " · ") + "ignored" }
        if shot.grindVote == "count" { line += (line.isEmpty ? "" : " · ") + "counted" }
        return line
    }
}
