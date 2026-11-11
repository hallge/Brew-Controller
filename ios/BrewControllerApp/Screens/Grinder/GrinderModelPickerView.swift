// The grinder model picker: a searchable list grouped by maker (web: grinderMaker + drawGrinder's optgroups).
import SwiftUI

enum GrinderMaker {
    /// The web page's grinderMaker.
    static func of(_ name: String) -> String {
        let known: [(String, String)] = [
            ("Barista Express", "Breville"), ("Breville", "Breville"), ("Baratza", "Baratza"), ("Eureka", "Eureka"),
            ("Niche", "Niche"), ("DF", "Turin (DF)"), ("1Zpresso", "1Zpresso"), ("Comandante", "Comandante"),
            ("Timemore", "Timemore"), ("Fellow", "Fellow"), ("Mahlk", "Mahlkönig"),
        ]
        if let hit = known.first(where: { name.hasPrefix($0.0) }) { return hit.1 }
        let first = name.split(separator: " ").first.map(String.init) ?? ""
        return first.isEmpty ? "Other" : first
    }
}

struct GrinderPickItem: Identifiable, Equatable {
    var tag: String
    var name: String
    var id: String { tag }
}

struct GrinderModelPickerView: View {
    var items: [GrinderPickItem]
    var selected: String
    /// The row for a grinder not in the list ("custom").
    var customLabel: String
    var onPick: (String) -> Void
    @Environment(\.dismiss) private var dismiss
    @State private var search = ""

    private var groups: [(maker: String, items: [GrinderPickItem])] {
        let wanted = search.trimmingCharacters(in: .whitespaces)
        let kept = wanted.isEmpty ? items : items.filter { $0.name.localizedCaseInsensitiveContains(wanted) || GrinderMaker.of($0.name).localizedCaseInsensitiveContains(wanted) }
        let grouped = Dictionary(grouping: kept) { GrinderMaker.of($0.name) }
        return grouped.keys.sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }.map { key in
            (maker: key, items: (grouped[key] ?? []).sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending })
        }
    }

    var body: some View {
        List {
            ForEach(groups, id: \.maker) { group in
                Section(group.maker) {
                    ForEach(group.items) { item in
                        row(item.name, tag: item.tag)
                    }
                }
            }
            if search.trimmingCharacters(in: .whitespaces).isEmpty {
                Section { row(customLabel, tag: "custom") }
            }
        }
        .searchable(text: $search, placement: .navigationBarDrawer(displayMode: .always), prompt: "Search grinders")
        .navigationTitle("Grinder model")
        .navigationBarTitleDisplayMode(.inline)
        .accessibilityIdentifier("grinderModelList")
    }

    private func row(_ title: String, tag: String) -> some View {
        Button {
            onPick(tag)
            dismiss()
        } label: {
            HStack {
                Text(title).foregroundStyle(.primary)
                Spacer()
                if tag == selected { Image(systemName: "checkmark").foregroundStyle(.tint) }
            }
        }
        .accessibilityIdentifier("grinderModelRow_\(tag)")
    }
}
