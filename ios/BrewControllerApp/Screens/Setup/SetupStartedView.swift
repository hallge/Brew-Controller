// Getting started (web: #start): the steps for a new controller and "when you want more". The board's
// Wi-Fi step is left out: the phone has no Wi-Fi to join.
import SwiftUI

struct SetupStartedView: View {
    @State private var start = SetupStart()
    @State private var loaded = false

    private struct Step: Identifiable {
        var id: Int
        var title: String
        var done: Bool
        var text: String
    }

    private var steps: [Step] {
        let x = start
        return [
            Step(id: 1, title: "Pair your scale", done: x.scalePaired,
                 text: x.scaleConnected ? "Connected." : x.scalePaired ? "Paired. Switch it on to connect." : "Switch the scale on near the phone: a supported scale is adopted by itself."),
            Step(id: 2, title: "Pair the brew bot", done: x.botPaired,
                 text: x.botPaired ? "Paired. It wakes when you press Start." : "Put the bot on the brew button, then pick it in the list of pushers (Devices)."),
            Step(id: 3, title: "Check your espresso machine", done: !x.machineMaker.isEmpty,
                 text: x.machineMaker.isEmpty ? "Choose your make and model: it says how the bot presses." : "\(x.machineMaker) \(x.machineModel). Change it if yours is different."),
            Step(id: 4, title: "Describe your grinder", done: x.grinderSet && x.grinderRange,
                 text: x.grinderSet ? (x.grinderModel.isEmpty ? "Your grinder" : x.grinderModel) + (x.grinderRange ? "." : ": add its range of settings for a first guess.")
                     : "Pick your grinder and how it is set: advice is given in its own settings."),
            Step(id: 5, title: "Choose a recipe", done: x.recipes > 0,
                 text: x.recipes > 0 ? "\(x.recipes) recipe\(x.recipes == 1 ? "" : "s") ready. Adjust dose, yield and time to taste." : "Add a recipe: dose, yield and brew time."),
            Step(id: 6, title: "Add your coffee", done: x.coffeesOnRecipe > 0,
                 text: x.coffeesOnRecipe > 0 ? "On the controller, ready to pick." : "Add the coffee you are brewing and tick a recipe for it."),
            Step(id: 7, title: "Pull your first shot", done: x.shots,
                 text: x.shots ? "Done. Grind advice learns from every shot." : "Cup on the scale, then Start. Grind advice starts from here."),
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
                    HStack(alignment: .top, spacing: 12) {
                        Image(systemName: step.done ? "checkmark.circle.fill" : (step.id == next ? "\(step.id).circle.fill" : "\(step.id).circle"))
                            .foregroundStyle(step.done ? Color.green : Color.accentColor)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(step.title).fontWeight(.semibold)
                            Text(step.text).font(.footnote).foregroundStyle(.secondary)
                        }
                    }
                    .accessibilityElement(children: .combine)
                    .accessibilityIdentifier("setupStartedStep\(step.id)")
                }
            }
            Section("When you want more") {
                moreRow("Power bot", "Switch the machine on and off, on a schedule too (Devices, then Machine).")
                moreRow("Insights and Discover", "Charts of your shots and coffees picked for your taste (Setup › Show in More).")
                moreRow("Shared library", "Share coffees and dial-ins, and start from what other owners settled on (Setup › Shared library).")
            }
        }
        .navigationTitle("Getting started")
        .task {
            if let value = try? await BrewAPI.shared.get("/api/start", as: SetupStart.self) { start = value }
            loaded = true
        }
    }

    private func moreRow(_ title: String, _ text: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title).fontWeight(.semibold)
            Text(text).font(.footnote).foregroundStyle(.secondary)
        }
    }
}
