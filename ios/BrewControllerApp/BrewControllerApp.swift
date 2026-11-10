import SwiftUI

@main
struct BrewControllerApp: App {
    @State private var session = ShotSession()

    var body: some Scene {
        WindowGroup {
            MainView(session: session)
        }
    }
}
