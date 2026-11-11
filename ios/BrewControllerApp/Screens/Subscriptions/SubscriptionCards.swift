// The cards of the Subscriptions page: a subscription of the owner's (with Manage, Arrived, Skip one,
// Edit) and a service of the shared library (with Add). Also used by Discover's recommendations.
import SwiftUI
import UIKit

enum SubscriptionColors {
    /// "#rrggbb" as a colour; nil for anything else.
    static func color(hex: String) -> Color? {
        guard hex.count == 7, hex.hasPrefix("#"), let value = UInt32(hex.dropFirst(), radix: 16) else { return nil }
        return Color(.sRGB, red: Double((value >> 16) & 0xFF) / 255, green: Double((value >> 8) & 0xFF) / 255,
                     blue: Double(value & 0xFF) / 255, opacity: 1)
    }

    /// A colour as "#rrggbb".
    static func hex(_ color: Color) -> String {
        var red: CGFloat = 0, green: CGFloat = 0, blue: CGFloat = 0, alpha: CGFloat = 0
        UIColor(color).getRed(&red, green: &green, blue: &blue, alpha: &alpha)
        return String(format: "#%02x%02x%02x", Int((red * 255).rounded()), Int((green * 255).rounded()),
                      Int((blue * 255).rounded()))
    }
}

/// The picture at the top of a card: the shop's picture over its colour, or the first letter of its name.
struct SubscriptionBanner: View {
    let name: String
    let color: String
    let image: String

    var body: some View {
        let base = SubscriptionColors.color(hex: color) ?? Color(.sRGB, red: 0.23, green: 0.43, blue: 0.62, opacity: 1)
        ZStack {
            LinearGradient(colors: [base, base.opacity(0.6)], startPoint: .topLeading, endPoint: .bottomTrailing)
            if let url = URL(string: image), !image.isEmpty {
                AsyncImage(url: url) { phase in
                    if let loaded = phase.image {
                        loaded.resizable().aspectRatio(contentMode: .fill)
                    } else {
                        initial
                    }
                }
            } else {
                initial
            }
        }
        .frame(height: 72)
        .clipped()
        .accessibilityHidden(true)
    }

    private var initial: some View {
        Text(String(name.trimmingCharacters(in: .whitespaces).prefix(1)).uppercased())
            .font(.system(size: 34, weight: .bold, design: .rounded))
            .foregroundStyle(Color.white.opacity(0.85))
    }
}

private struct SubscriptionPill: View {
    let text: String
    var warn = false

    var body: some View {
        let palette = BrewTheme.shared.palette
        Text(text)
            .font(.caption2.weight(.semibold))
            .padding(.horizontal, 7).padding(.vertical, 2)
            .background((warn ? palette.accent : palette.track).opacity(0.8), in: Capsule())
            .foregroundStyle(palette.text)
    }
}

/// A subscription the owner keeps.
struct SubscriptionCard: View {
    let item: SubscriptionItem
    let shotsPerDay: Double
    /// "On its way: Guji, expected Nov 20" lines.
    let onTheWay: [String]
    let onArrived: () -> Void
    let onSkip: () -> Void
    let onEdit: () -> Void

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 0) {
            SubscriptionBanner(name: item.displayName, color: item.color, image: item.image)
            VStack(alignment: .leading, spacing: 5) {
                HStack(spacing: 6) {
                    Text(item.displayName).font(.headline).foregroundStyle(palette.text)
                    Spacer(minLength: 0)
                    if !item.isActive { SubscriptionPill(text: item.status == "paused" ? "Paused" : "Cancelled") }
                    if item.isActive && item.dueSoon {
                        SubscriptionPill(text: (item.daysUntil ?? 0) < 0 ? "Overdue" : "Soon", warn: true)
                    }
                    if item.share != "local" {
                        SubscriptionPill(text: item.share == "published" ? "Library"
                                         : (SubscriptionWording.share[item.share] ?? item.share))
                    }
                }
                let source = [item.site != item.name ? item.site : "",
                              SubscriptionWording.platform[item.platform].map { "via " + $0 } ?? ""]
                    .filter { !$0.isEmpty }.joined(separator: " · ")
                if !source.isEmpty { Text(source).font(.footnote).foregroundStyle(palette.dim) }
                let bits = [SubscriptionWording.kindText(item.kind), SubscriptionWording.every(days: item.everyDays),
                            item.priceCents > 0 ? SubscriptionWording.money(cents: item.priceCents) : ""]
                    .filter { !$0.isEmpty }.joined(separator: " · ")
                Text(bits).font(.subheadline).foregroundStyle(palette.dim)
                if item.nextDate != 0 {
                    let when = SubscriptionWording.when(item)
                    Text("Next: " + (when.isEmpty ? SubscriptionWording.date(item.nextDate) : when))
                        .font(.subheadline).foregroundStyle(palette.text)
                }
                ForEach(onTheWay, id: \.self) { line in
                    Text(line).font(.footnote).foregroundStyle(palette.accent)
                }
                if item.kind == "supplies" && (item.stock > 0 || item.perDelivery > 0) { supplies }
                if !item.note.isEmpty { Text(item.note).font(.footnote).foregroundStyle(palette.dim) }
                if item.lastArrived != 0 {
                    Text("Last arrived " + SubscriptionWording.date(item.lastArrived))
                        .font(.footnote).foregroundStyle(palette.dim)
                }
                actions.padding(.top, 6)
            }
            .padding(12)
        }
        .background(palette.panel, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        .opacity(item.isActive ? 1 : 0.7)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("subscriptionCard.\(item.id)")
    }

    private var suppliesLine: String {
        var line = "\(item.stock) \(item.item.isEmpty ? "left" : item.item)"
        if let days = item.stockDaysLeft {
            line += " · about \(days) days"
            if shotsPerDay > 0 { line += " at " + SubscriptionWording.perDay(shotsPerDay) }
        }
        return line
    }

    private var supplies: some View {
        let palette = BrewTheme.shared.palette
        let full = Double(max(item.perDelivery, item.stock, 1))
        let fraction = min(1, Double(item.stock) / full)
        return VStack(alignment: .leading, spacing: 3) {
            Text(suppliesLine).font(.footnote).foregroundStyle(palette.text)
            ProgressView(value: fraction).tint(item.stockShort ? palette.accent : palette.ok)
            if item.stockShort {
                Text("Runs out before the next delivery").font(.footnote).foregroundStyle(palette.accent)
            }
        }
    }

    private var actions: some View {
        HStack(spacing: 8) {
            if let url = URL(string: item.manageUrl.isEmpty ? item.url : item.manageUrl),
               !(item.manageUrl.isEmpty && item.url.isEmpty) {
                Link(item.manageUrl.isEmpty ? "Open" : "Manage", destination: url)
                    .buttonStyle(.bordered)
                    .accessibilityIdentifier("subscriptionManage.\(item.id)")
            }
            if item.isActive {
                Button("Arrived", action: onArrived)
                    .buttonStyle(.bordered)
                    .accessibilityIdentifier("subscriptionArrived.\(item.id)")
                Button("Skip one", action: onSkip)
                    .buttonStyle(.bordered)
                    .accessibilityIdentifier("subscriptionSkip.\(item.id)")
            }
            Spacer(minLength: 0)
            Button("Edit", action: onEdit)
                .accessibilityIdentifier("subscriptionEdit.\(item.id)")
        }
        .font(.subheadline)
    }
}

/// A service of the shared library, with what it offers and its reviews.
struct SubscriptionLibraryCard: View {
    let service: SubscriptionLibraryService
    /// Reasons from the recommendations ("" none).
    var why: [String] = []
    var people = ""
    /// The owner has it already.
    let have: Bool
    let onAdd: () -> Void

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 0) {
            SubscriptionBanner(name: service.name, color: service.color, image: service.image)
            VStack(alignment: .leading, spacing: 5) {
                HStack {
                    Text(service.name).font(.headline).foregroundStyle(palette.text)
                    Spacer(minLength: 0)
                    SubscriptionPill(text: "Library")
                }
                let bits = [SubscriptionWording.kindText(service.kind), SubscriptionWording.cadenceText(service.cadences),
                            SubscriptionWording.priceFrom(cents: service.priceFromCents, currency: service.currency)]
                    .filter { !$0.isEmpty }.joined(separator: " · ")
                Text(bits).font(.subheadline).foregroundStyle(palette.dim)
                if !service.shipsTo.isEmpty {
                    Text("Ships to " + service.shipsTo.joined(separator: ", ")).font(.footnote).foregroundStyle(palette.dim)
                }
                if let reviews = service.reviews, reviews.n > 0 {
                    let filled = Int(reviews.stars.rounded())
                    Text(String(repeating: "★", count: max(0, min(5, filled))) + String(repeating: "☆", count: max(0, 5 - filled))
                         + " " + BrewFormat.decimal(reviews.stars, 1) + " from \(reviews.n) reviews")
                        .font(.footnote).foregroundStyle(palette.accent)
                } else {
                    Text("Not enough reviews yet").font(.footnote).foregroundStyle(palette.dim)
                }
                if !service.about.isEmpty { Text(service.about).font(.footnote).foregroundStyle(palette.dim) }
                let tags = service.style.compactMap { SubscriptionWording.style[$0] }
                    + service.sourcing.compactMap { SubscriptionWording.sourcing[$0] }
                if !tags.isEmpty {
                    Text(tags.joined(separator: " · ")).font(.caption).foregroundStyle(palette.text)
                }
                if !why.isEmpty {
                    Text(why.joined(separator: " · ")).font(.caption).foregroundStyle(palette.accent)
                }
                if !people.isEmpty { Text(people).font(.footnote).foregroundStyle(palette.dim) }
                HStack {
                    if have {
                        Text("You have this one").font(.footnote).foregroundStyle(palette.dim)
                    } else {
                        Button("Add to my subscriptions", action: onAdd)
                            .buttonStyle(.borderedProminent)
                            .accessibilityIdentifier("subscriptionLibraryAdd.\(service.id)")
                    }
                    Spacer(minLength: 0)
                    if let url = URL(string: service.url), !service.url.isEmpty {
                        Link("Visit", destination: url)
                    }
                }
                .font(.subheadline)
                .padding(.top, 4)
            }
            .padding(12)
        }
        .background(palette.panel, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .clipShape(RoundedRectangle(cornerRadius: 14, style: .continuous))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("subscriptionLibraryCard.\(service.id)")
    }
}
