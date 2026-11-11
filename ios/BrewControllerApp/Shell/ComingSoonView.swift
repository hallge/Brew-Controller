// A placeholder for a screen that is not built yet. Each later screen replaces its own placeholder in
// its own folder (Screens/<Name>/); nothing else refers to this view.
import SwiftUI

struct ComingSoonView: View {
    var title: String

    var body: some View {
        ZStack {
            pal.bg.ignoresSafeArea()
            VStack(spacing: 10) {
                Text(title).font(.title.weight(.semibold)).foregroundStyle(pal.text)
                Text("Coming soon").foregroundStyle(pal.dim)
            }
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("comingSoon-\(title)")
        }
    }
}
