// One shot by its number, for the screens that link to a shot (the Brew tab, Insights, the Grinder charts):
// it makes and loads its own store, so the caller needs only the id. A shot that is not in the history says so.
import SwiftUI

struct ShotsDetailScreen: View {
    let shotID: Int

    @State private var store = ShotsStore()

    var body: some View {
        ShotsDetailView(store: store, shotID: shotID)
            .task { await store.load() }
            .onChange(of: LiveModel.shared.live.lastShotID) { _, _ in Task { await store.load() } }
    }
}
