// Compare up to 8 shots: their weight curves (solid) and optionally flow (dashed), each in its own colour,
// optionally lined up at the first drops (WebPage.h #cmpcard, `drawCompare`).
import SwiftUI

struct ShotsCompareView: View {
    let store: ShotsStore

    @State private var showFlow = false
    @State private var align = false
    @State private var curves: [Int: ShotDataModel] = [:]
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        List {
            Section {
                if store.compareIDs.isEmpty {
                    Text("No shots to compare. Open a shot and choose Add to comparison; up to 8.")
                        .foregroundStyle(.secondary)
                } else {
                    ShotsCompareChart(lines: lines, showFlow: showFlow)
                        .padding(.vertical, 6)
                }
                Toggle("Flow", isOn: $showFlow).accessibilityIdentifier("shotsCompareFlow")
                Toggle("Line up at first drops", isOn: $align).accessibilityIdentifier("shotsCompareAlign")
            } footer: {
                Text("Solid: weight in the cup. Dashed: flow (right-hand scale). Open a shot and press \"Add to comparison\" to add it; up to 8.")
            }

            Section("Shots") {
                ForEach(Array(store.compareIDs.enumerated()), id: \.element) { index, id in
                    if let shot = store.shot(id: id) {
                        HStack {
                            Circle().fill(ShotsPlot.color(index)).frame(width: 12, height: 12)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(ShotsText.when(shot) + " · " + (shot.coffee.isEmpty ? shot.recipe : shot.coffee))
                                    .font(.subheadline)
                                Text(BrewFormat.grams(shot.finalWeightG) + " g in " + BrewFormat.seconds(shot.shotTimeS) + " s")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                            Spacer()
                            Button("Remove") { store.toggleCompare(id) }
                                .buttonStyle(.borderless)
                                .accessibilityIdentifier("shotsCompareRemove-\(id)")
                        }
                    }
                }
            }

            Section {
                Button("Clear", role: .destructive) {
                    store.clearCompare()
                    dismiss()
                }
                .accessibilityIdentifier("shotsCompareClear")
            }
        }
        .accessibilityIdentifier("shotsCompare")
        .shotsThemed()
        .navigationTitle("Compare shots")
        .navigationBarTitleDisplayMode(.inline)
        .task(id: store.compareIDs) { await loadCurves() }
        .shotsNotices(store)
    }

    /// The lines to draw, from the curves read so far.
    private var lines: [ShotsCompareChart.Line] {
        store.compareIDs.enumerated().compactMap { index, id in
            guard let shot = store.shot(id: id), let curve = curves[id] else { return nil }
            return ShotsCompareChart.Line(
                id: id,
                name: ShotsText.when(shot),
                color: ShotsPlot.color(index),
                weight: ShotsPlot.points(curve.timeS, curve.weightG, limit: 300),
                flow: ShotsPlot.points(curve.timeS, curve.flowGS, limit: 300),
                offset: align && shot.firstDropS > 0 ? shot.firstDropS : 0)
        }
    }

    private func loadCurves() async {
        for id in store.compareIDs where curves[id] == nil {
            if let curve = await store.curve(id: id) { curves[id] = curve }
        }
    }
}
