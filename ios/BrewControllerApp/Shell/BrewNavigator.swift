// Where the app is asked to go from a place that does not own the destination: the selected tab, and a
// screen the Brew tab pushes (an open shot, Getting started, Share to Reddit, Discover). The Brew tab
// shows `route` with navigationDestination; the shell shows `tab`.
import Observation

enum BrewTabID: Hashable {
    case brew, shots, coffees, grinder, more
}

/// A screen the Brew tab pushes inside its own NavigationStack.
enum BrewRoute: Hashable {
    case shot(Int)
    case started
    case reddit(Int)
    case discover
    case community
}

@MainActor
@Observable
final class BrewNavigator {
    static let shared = BrewNavigator()

    var tab: BrewTabID = .brew
    var route: BrewRoute?

    func select(_ tab: BrewTabID) { self.tab = tab }

    func open(_ route: BrewRoute) {
        tab = .brew
        self.route = route
    }
}
