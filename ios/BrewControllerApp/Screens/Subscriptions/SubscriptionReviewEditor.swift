// "Your review" on the subscription form: who, stars, six aspects and a note, saved per drinker
// (POST /api/subscriptions/review, /review/delete), with the reviews already given listed under it.
import SwiftUI

struct SubscriptionReviewEditor: View {
    let store: SubscriptionsStore
    let subscription: Int
    let shareRatings: Bool
    /// The service is in the shared library.
    let shared: Bool

    @State private var drinker = 0
    @State private var stars = 0
    @State private var aspects: [Int] = Array(repeating: 0, count: SubscriptionWording.aspects.count)
    @State private var note = ""
    @State private var loaded = false

    private var existing: SubscriptionReview? {
        store.reviews.first { $0.subscription == subscription && $0.drinker == drinker }
    }

    var body: some View {
        let palette = BrewTheme.shared.palette
        let people = store.drinkers.people
        VStack(alignment: .leading, spacing: 10) {
            Text(shareRatings ? (shared ? "Shared anonymously with the library" : "Shared once the service is in the library")
                 : "Kept on this controller")
                .font(.footnote).foregroundStyle(palette.dim)
            if !people.isEmpty {
                Picker("Who is reviewing", selection: $drinker) {
                    ForEach(people, id: \.id) { person in Text(person.name).tag(person.id) }
                }
                .onChange(of: drinker) { _, _ in fill() }
                .accessibilityIdentifier("subscriptionReviewWho")
            }
            HStack(spacing: 6) {
                ForEach(1...5, id: \.self) { n in
                    Button {
                        stars = n
                    } label: {
                        Image(systemName: n <= stars ? "star.fill" : "star")
                            .foregroundStyle(n <= stars ? palette.accent : palette.dim)
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("\(n) star\(n == 1 ? "" : "s")")
                    .accessibilityIdentifier("subscriptionReviewStar\(n)")
                }
                if stars == 0 { Text("tap to rate").font(.footnote).foregroundStyle(palette.dim) }
            }
            ForEach(Array(SubscriptionWording.aspects.enumerated()), id: \.offset) { entry in
                VStack(alignment: .leading, spacing: 0) {
                    HStack {
                        Text(entry.element.label).font(.subheadline)
                        Spacer()
                        Text(aspects[entry.offset] > 0 ? "\(aspects[entry.offset]) / 5" : "not given")
                            .font(.footnote).foregroundStyle(palette.dim)
                    }
                    Slider(value: Binding(get: { Double(aspects[entry.offset]) },
                                          set: { aspects[entry.offset] = Int($0.rounded()) }),
                           in: 0...5, step: 1)
                        .accessibilityLabel(entry.element.label)
                }
            }
            TextField("Note (stays on this controller)", text: $note)
                .onChange(of: note) { _, text in if text.count > 39 { note = String(text.prefix(39)) } }
                .accessibilityIdentifier("subscriptionReviewNote")
            HStack {
                Button("Save review") {
                    Task { _ = await store.saveReview(subscription: subscription, drinker: drinker, stars: stars,
                                                      aspects: aspectDictionary, note: note.trimmingCharacters(in: .whitespaces)) }
                }
                .buttonStyle(.borderedProminent)
                .accessibilityIdentifier("subscriptionReviewSave")
                if let existing {
                    Button("Delete review", role: .destructive) {
                        Task {
                            if await store.deleteReview(existing) { reset() }
                        }
                    }
                    .buttonStyle(.bordered)
                    .accessibilityIdentifier("subscriptionReviewDelete")
                }
            }
            let all = store.reviews(for: subscription)
            if all.count > 1 || (!all.isEmpty && store.drinkers.named) {
                ForEach(all) { review in
                    Text(name(of: review.drinker) + ": " + String(repeating: "★", count: max(0, min(5, review.stars)))
                         + (review.note.isEmpty ? "" : " " + review.note))
                        .font(.footnote).foregroundStyle(palette.dim)
                }
            }
        }
        .onAppear {
            guard !loaded else { return }
            loaded = true
            if let first = people.first { drinker = first.id }
            fill()
        }
    }

    private var aspectDictionary: [String: Int] {
        var result: [String: Int] = [:]
        for (index, aspect) in SubscriptionWording.aspects.enumerated() where aspects[index] > 0 {
            result[aspect.key] = aspects[index]
        }
        return result
    }

    private func name(of drinker: Int) -> String {
        if drinker == 0 { return "Household" }
        return drinker - 1 < store.drinkers.drinkers.count && !store.drinkers.drinkers[drinker - 1].isEmpty
            ? store.drinkers.drinkers[drinker - 1] : "Drinker \(drinker)"
    }

    /// The form for the drinker's existing review (or empty).
    private func fill() {
        if let review = existing {
            stars = review.stars
            aspects = SubscriptionWording.aspects.map { review.aspects[$0.key] ?? 0 }
            note = review.note
        } else {
            reset()
        }
    }

    private func reset() {
        stars = 0
        aspects = Array(repeating: 0, count: SubscriptionWording.aspects.count)
        note = ""
    }
}
