// Getting started (web: #start): the steps for a new controller and "when you want more". The board's
// Wi-Fi step is left out: the phone has no Wi-Fi to join.
import SwiftUI

struct SetupStartedScreen: View {
    @State private var start = SetupStart()
    @State private var shareStore = SetupStore()
    @State private var loaded = false

    private struct Step: Identifiable {
        var id: Int
        var title: String
        var done: Bool
        var text: String
        var target: Target?
    }

    /// Where a step or card leads. The first shot is made on the Brew tab, which has no link from here.
    private enum Target { case devices, machine, grinder, recipes, coffees, insights, discover, sharing }

    private var steps: [Step] {
        let x = start
        return [
            Step(id: 1, title: "Pair your scale", done: x.scalePaired,
                 text: x.scaleConnected ? "Connected." : x.scalePaired ? "Paired. Switch it on to connect." : "Switch the scale on near the phone: a supported scale is adopted by itself.", target: .devices),
            Step(id: 2, title: "Pair the brew bot", done: x.botPaired,
                 text: x.botPaired ? "Paired. It wakes when you press Start." : "Put the bot on the brew button, then pick it in the list of pushers (Devices).", target: .devices),
            Step(id: 3, title: "Check your espresso machine", done: !x.machineMaker.isEmpty,
                 text: x.machineMaker.isEmpty ? "Choose your make and model: it says how the bot presses." : "\(x.machineMaker) \(x.machineModel). Change it if yours is different.", target: .machine),
            Step(id: 4, title: "Describe your grinder", done: x.grinderSet && x.grinderRange,
                 text: x.grinderSet ? (x.grinderModel.isEmpty ? "Your grinder" : x.grinderModel) + (x.grinderRange ? "." : ": add its range of settings for a first guess.")
                     : "Pick your grinder and how it is set: advice is given in its own settings.", target: .grinder),
            Step(id: 5, title: "Choose a recipe", done: x.recipes > 0,
                 text: x.recipes > 0 ? "\(x.recipes) recipe\(x.recipes == 1 ? "" : "s") ready. Adjust dose, yield and time to taste." : "Add a recipe: dose, yield and brew time.", target: .recipes),
            Step(id: 6, title: "Add your coffee", done: x.coffeesOnRecipe > 0,
                 text: x.coffeesOnRecipe > 0 ? "On the controller, ready to pick." : "Add the coffee you are brewing and tick a recipe for it.", target: .coffees),
            Step(id: 7, title: "Pull your first shot", done: x.shots,
                 text: x.shots ? "Done. Grind advice learns from every shot." : "On the Brew tab: cup on the scale, then Start. Grind advice starts from here."),
        ]
    }

    var body: some View {
        let list = steps
        let doneCount = list.filter(\.done).count
        let next = list.first { !$0.done }?.id
        return List {
            Section {
                Text("\(doneCount) of \(list.count) done")
                    .fontWeight(.semibold)
                    .accessibilityIdentifier("setupStartedCount")
                ProgressView(value: Double(doneCount), total: Double(list.count))
            }
            Section {
                ForEach(list) { step in
                    stepRow(step, current: step.id == next)
                }
            }
            Section("When you want more") {
                moreRow("Power bot", "Switch the machine on and off, on a schedule too (Devices, then Machine).", .machine)
                moreRow("Insights", "Charts of your shots and your taste profile (Setup › Show in More).", .insights)
                moreRow("Discover", "Coffees picked for your taste (Setup › Show in More).", .discover)
                moreRow("Shared library", "Share coffees and dial-ins, and start from what other owners settled on (Setup › Shared library).", .sharing)
            }
        }
        .navigationTitle("Getting started")
        .task {
            if let value = try? await BrewAPI.shared.get("/api/start", as: SetupStart.self) { start = value }
            loaded = true
        }
    }

    @ViewBuilder
    private func stepRow(_ step: Step, current: Bool) -> some View {
        let row = HStack(alignment: .top, spacing: 12) {
            Image(systemName: step.done ? "checkmark.circle.fill" : (current ? "\(step.id).circle.fill" : "\(step.id).circle"))
                .foregroundStyle(step.done ? Color.green : Color.accentColor)
            VStack(alignment: .leading, spacing: 2) {
                Text(step.title).fontWeight(.semibold)
                Text(step.text).font(.footnote).foregroundStyle(.secondary)
            }
        }
        if let target = step.target {
            NavigationLink { destination(target) } label: { row }
                .accessibilityIdentifier("setupStartedStep\(step.id)")
        } else {
            row.accessibilityElement(children: .combine).accessibilityIdentifier("setupStartedStep\(step.id)")
        }
    }

    private func moreRow(_ title: String, _ text: String, _ target: Target) -> some View {
        NavigationLink {
            destination(target)
        } label: {
            VStack(alignment: .leading, spacing: 2) {
                Text(title).fontWeight(.semibold)
                Text(text).font(.footnote).foregroundStyle(.secondary)
            }
        }
        .accessibilityIdentifier("setupStartedMore_\(title)")
    }

    @ViewBuilder
    private func destination(_ target: Target) -> some View {
        switch target {
        case .devices: DevicesScreen()
        case .machine: MachineScreen()
        case .grinder: GrinderTab()
        case .recipes: RecipesScreen()
        case .coffees: CoffeesTab()
        case .insights: InsightsScreen()
        case .discover: DiscoverScreen()
        case .sharing: SetupSharingView(store: shareStore)
        }
    }
}
