// "Your coffee taste": two radar charts (the five cup values and the eight flavour families), the
// drinker switch, a line of notes and "Liked best". From GET /api/taste/profile.
import SwiftUI

struct InsightsTasteCard: View {
    let profile: InsightsTasteProfile?
    let ratings: InsightsRatings
    let drinker: Int
    let coffeeName: (Int) -> String?
    let onChoose: (Int) -> Void

    var body: some View {
        let palette = BrewTheme.shared.palette
        InsightsCard(title: "Your coffee taste", note: "", identifier: "insightsTaste") {
            let people = ratings.people
            if !people.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        chip("Household", id: 0)
                        ForEach(people, id: \.id) { person in
                            chip(person.name, id: person.id)
                        }
                    }
                }
                .accessibilityIdentifier("insightsTasteDrinkers")
            }
            if let profile {
                Text("The cup").font(.footnote).foregroundStyle(palette.dim)
                InsightsRadar(labels: profile.cupLabels, values: profile.cupValues, known: profile.cupKnown)
                    .accessibilityIdentifier("insightsTasteCupRadar")
                Text("Flavours").font(.footnote).foregroundStyle(palette.dim)
                InsightsRadar(labels: profile.flavourLabels, values: profile.flavourValues, known: profile.flavourKnown)
                    .accessibilityIdentifier("insightsTasteFlavourRadar")
                Text(InsightsMath.tasteNote(profile))
                    .font(.subheadline)
                    .foregroundStyle(profile.profile.coffees == 0 ? palette.dim : palette.text)
                    .accessibilityIdentifier("insightsTasteNote")
                if !profile.best.isEmpty {
                    Text("Liked best").font(.subheadline.weight(.semibold)).foregroundStyle(palette.text)
                    ForEach(Array(profile.best.enumerated()), id: \.offset) { entry in
                        if let name = coffeeName(entry.element.coffee) {
                            HStack {
                                Text(name).foregroundStyle(palette.text)
                                Spacer()
                                Text(String(repeating: "★", count: max(0, min(5, entry.element.stars))))
                                    .foregroundStyle(palette.accent)
                            }
                            .font(.subheadline)
                        }
                    }
                }
                InsightsExplainer(
                    summary: "From your coffee ratings: coffees you loved pull it their way, ones you disliked push it away.",
                    lines: [
                        "The middle ring is no lean either way; further out is more of it, further in less.",
                        "A pale point says little is known about it yet. The profile shows from 3 rated coffees and is settled from 8.",
                        "Values you did not give are filled in from each coffee's tasting notes, process and roast level, with less weight.",
                    ])
            } else {
                InsightsEmptyNote(text: "The taste profile cannot be read from the controller.",
                                  identifier: "insightsTasteEmpty")
            }
        }
    }

    private func chip(_ title: String, id: Int) -> some View {
        let palette = BrewTheme.shared.palette
        return Button(title) { onChoose(id) }
            .buttonStyle(.bordered)
            .tint(id == drinker ? palette.accent : palette.dim)
            .accessibilityAddTraits(id == drinker ? .isSelected : [])
            .accessibilityIdentifier("insightsTasteDrinker\(id)")
    }
}
