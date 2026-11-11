// Subscriptions (web page: #subscriptions). Pushed by the More tab inside its NavigationStack.
// The web page's "Update from the shops" helper needs a Chrome extension and has no iPhone version:
// "Read the page" on the form covers it.
import SwiftUI

struct SubscriptionsScreen: View {
    @State private var store = SubscriptionsStore()
    @State private var editing: SubscriptionFormTarget?
    @State private var arriving: SubscriptionItem?
    @State private var pending: SubscriptionPending?
    @State private var query = ""
    @Environment(\.openURL) private var openURL

    private enum SubscriptionPending: Identifiable {
        case arrived(SubscriptionItem)
        case skip(SubscriptionItem)

        var id: String {
            switch self {
            case .arrived(let item): return "arrived\(item.id)"
            case .skip(let item): return "skip\(item.id)"
            }
        }
    }

    var body: some View {
        let palette = BrewTheme.shared.palette
        ScrollView {
            VStack(spacing: 14) {
                if let notice = store.notice {
                    Text(notice.text)
                        .font(.subheadline)
                        .foregroundStyle(palette.text)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(12)
                        .background((notice.good ? palette.ok : palette.bad).opacity(0.25),
                                    in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                        .accessibilityIdentifier("subscriptionNotice")
                }
                switch store.phase {
                case .loading:
                    ProgressView("Loading subscriptions").frame(maxWidth: .infinity, minHeight: 140)
                        .accessibilityIdentifier("subscriptionsLoading")
                case .failed(let message):
                    SubscriptionPanel(title: "Subscriptions", identifier: "subscriptionsFailed") {
                        Text(message).font(.subheadline).foregroundStyle(palette.dim)
                        Button("Try again") { Task { await store.load() } }.buttonStyle(.borderedProminent)
                    }
                case .loaded:
                    loadedContent
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
        }
        .background(palette.bg)
        .navigationTitle("Subscriptions")
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) {
                Button {
                    editing = SubscriptionFormTarget(id: 0)
                } label: {
                    Label("Add a subscription", systemImage: "plus")
                }
                .accessibilityIdentifier("subscriptionAdd")
            }
        }
        .refreshable { await store.load() }
        .task { await store.load() }
        .task(id: store.notice?.id) {
            guard store.notice != nil else { return }
            try? await Task.sleep(for: .seconds(5))
            if !Task.isCancelled { store.notice = nil }
        }
        .sheet(item: $editing) { target in
            SubscriptionFormView(store: store, target: target)
        }
        .sheet(item: $arriving) { item in
            SubscriptionArrivalView(store: store, item: item)
        }
        .alert(confirmTitle, isPresented: Binding(get: { pending != nil }, set: { if !$0 { pending = nil } }),
               presenting: pending) { action in
            switch action {
            case .arrived(let item):
                Button("Arrived") { Task { _ = await store.markArrived(item) } }
            case .skip(let item):
                Button("Skip") {
                    Task {
                        if await store.skip(item), !item.manageUrl.isEmpty, let url = URL(string: item.manageUrl) {
                            openURL(url)
                        }
                    }
                }
            }
            Button("Cancel", role: .cancel) {}
        } message: { action in
            Text(confirmMessage(action))
        }
        .accessibilityIdentifier("subscriptionsScreen")
    }

    private var confirmTitle: String {
        switch pending {
        case .arrived: return "Arrived?"
        case .skip: return "Skip one?"
        case nil: return ""
        }
    }

    private func confirmMessage(_ action: SubscriptionPending) -> String {
        switch action {
        case .arrived(let item):
            if item.kind == "supplies" && item.perDelivery > 0 {
                return "Add \(item.perDelivery) \(item.item) to the stock and move the next date on?"
            }
            return "Mark \"\(item.name)\" arrived and move the next date on?"
        case .skip(let item):
            return "Skip the next one of \"\(item.name)\"?\n\nThe date moves on here only: skip it on the shop's page too."
        }
    }

    // MARK: Content

    @ViewBuilder
    private var loadedContent: some View {
        let palette = BrewTheme.shared.palette
        summary
        reminders
        if store.list.subscriptions.isEmpty {
            SubscriptionPanel(title: "Keep your subscriptions in one place.", identifier: "subscriptionsEmpty") {
                Text("Coffee, paper filters, anything that comes by post: add the link and the controller builds a card, tracks when the next one comes, what it costs, and how many filters you have left.")
                    .font(.subheadline).foregroundStyle(palette.dim)
                Button("Add a subscription") { editing = SubscriptionFormTarget(id: 0) }
                    .buttonStyle(.borderedProminent)
                    .accessibilityIdentifier("subscriptionEmptyAdd")
            }
        } else {
            ForEach(store.ordered) { item in
                SubscriptionCard(
                    item: item, shotsPerDay: store.list.shotsPerDay,
                    onTheWay: store.bagsOnTheWay(for: item).map { bag in
                        "On its way: " + (store.coffeeName(bag.coffeeId) ?? "a bag")
                            + (bag.expectedDate != 0 ? ", expected " + SubscriptionWording.date(bag.expectedDate) : "")
                    },
                    onArrived: {
                        if item.kind == "coffee" { arriving = item } else { pending = .arrived(item) }
                    },
                    onSkip: { pending = .skip(item) },
                    onEdit: { editing = SubscriptionFormTarget(id: item.id) })
            }
        }
        sharedLibrary
        SubscriptionPanel(title: "Why skip and pause happen on the shop's page.", identifier: "subscriptionsWhy") {
            Text("• Shops run subscriptions on systems (Recharge, Ordergroove and the like) that only the shop can reach: there is no way in for your controller.\n• So the card links to where you manage it, and keeps its own track here: when it comes, what it costs, how much is left.\n• Arrived and Skip move the date on here. Skipping at the shop is still yours to do.\n• The picture and name come from the shop's page, read once by the controller when you add the link.")
                .font(.footnote).foregroundStyle(palette.dim)
        }
    }

    @ViewBuilder
    private var summary: some View {
        let list = store.list
        if !list.subscriptions.isEmpty {
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 8) {
                SubscriptionTile(title: "Each month", value: SubscriptionWording.money(cents: list.monthlyCents),
                                 detail: "\(store.active.count) active")
                    .accessibilityIdentifier("subscriptionTileMonth")
                if let next = store.nextUp {
                    SubscriptionTile(title: "Next up", value: next.displayName, detail: SubscriptionWording.when(next))
                        .accessibilityIdentifier("subscriptionTileNext")
                }
                ForEach(store.supplies) { supply in
                    SubscriptionTile(
                        title: SubscriptionWording.capitalized(supply.item.isEmpty ? supply.name : supply.item)
                            + (supply.note.isEmpty ? "" : " · " + supply.note),
                        value: "\(supply.stock) left",
                        detail: supply.stockDaysLeft.map { "about \($0) days at " + SubscriptionWording.perDay(list.shotsPerDay) }
                            ?? "not used up yet",
                        warn: supply.stockShort)
                        .accessibilityIdentifier("subscriptionTileStock.\(supply.id)")
                }
            }
        }
    }

    @ViewBuilder
    private var reminders: some View {
        let palette = BrewTheme.shared.palette
        ForEach(store.reminders) { item in
            HStack(alignment: .center, spacing: 8) {
                reminderText(item)
                .font(.subheadline)
                .foregroundStyle(palette.text)
                .frame(maxWidth: .infinity, alignment: .leading)
                if let url = URL(string: item.manageUrl), !item.manageUrl.isEmpty {
                    Link("Manage", destination: url).buttonStyle(.bordered)
                }
                if item.dueSoon, (item.daysUntil ?? 0) <= 0 {
                    Button("Arrived") {
                        if item.kind == "coffee" { arriving = item } else { pending = .arrived(item) }
                    }
                    .buttonStyle(.bordered)
                }
            }
            .padding(12)
            .background(palette.accent.opacity(0.18), in: RoundedRectangle(cornerRadius: 12, style: .continuous))
            .accessibilityIdentifier("subscriptionReminder.\(item.id)")
        }
    }

    /// "**Demo Roasters** charges or ships in 3 days. Skip it at the shop if you do not need it."
    private func reminderText(_ item: SubscriptionItem) -> Text {
        if item.dueSoon {
            let days = item.daysUntil ?? 0
            let rest: String
            if days < 0 {
                rest = "was due \(SubscriptionWording.date(item.nextDate)): did it come?"
            } else if days == 0 {
                rest = "charges or ships today"
            } else {
                rest = "charges or ships " + (days == 1 ? "tomorrow" : "in \(days) days")
                    + ". Skip it at the shop if you do not need it."
            }
            return Text("\(Text(item.name).bold()) \(rest)")
        }
        let what = SubscriptionWording.capitalized(item.item.isEmpty ? item.name : item.item)
        return Text("\(Text(what).bold()) will run out before the next delivery.")
    }

    private var filteredServices: [SubscriptionLibraryService] {
        let needle = query.lowercased().trimmingCharacters(in: .whitespaces)
        guard !needle.isEmpty else { return store.services }
        return store.services.filter { service in
            let tags = service.style.compactMap { SubscriptionWording.style[$0] }
                + service.sourcing.compactMap { SubscriptionWording.sourcing[$0] }
            return ([service.name, service.about, service.kind] + tags).joined(separator: " ").lowercased().contains(needle)
        }
    }

    @ViewBuilder
    private var sharedLibrary: some View {
        let palette = BrewTheme.shared.palette
        SubscriptionPanel(title: "From the shared library", identifier: "subscriptionsLibrary") {
            if !store.services.isEmpty {
                Text("\(store.services.count) service\(store.services.count == 1 ? "" : "s")")
                    .font(.footnote).foregroundStyle(palette.dim)
                TextField("Search services: name, style, sourcing", text: $query)
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("subscriptionsLibrarySearch")
            }
            let services = filteredServices
            if services.isEmpty {
                Text(store.services.isEmpty
                     ? "The shared library has no services yet, or it cannot be reached from here. Add yours above, then open it and send it to the library: once a reviewer lists it, everyone can add it in one tap."
                     : "No service matches.")
                    .font(.subheadline).foregroundStyle(palette.dim)
                    .accessibilityIdentifier("subscriptionsLibraryEmpty")
            } else {
                ForEach(services) { service in
                    SubscriptionLibraryCard(service: service, have: store.hasLibraryService(service)) {
                        Task {
                            if let id = await store.add(service) { editing = SubscriptionFormTarget(id: id) }
                        }
                    }
                }
            }
        }
    }
}

/// A card with a title (the screen's panels).
struct SubscriptionPanel<Content: View>: View {
    let title: String
    let identifier: String
    @ViewBuilder let content: () -> Content

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline).foregroundStyle(palette.text).accessibilityAddTraits(.isHeader)
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(palette.panel, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(identifier)
    }
}

/// A summary tile: each month, next up, stock.
struct SubscriptionTile: View {
    let title: String
    let value: String
    let detail: String
    var warn = false

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 2) {
            Text(title).font(.caption).foregroundStyle(palette.dim).lineLimit(1)
            Text(value).font(.title3.weight(.semibold)).foregroundStyle(palette.text).lineLimit(1)
            Text(detail).font(.caption2).foregroundStyle(warn ? palette.accent : palette.dim).lineLimit(2)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(palette.panel, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .accessibilityElement(children: .combine)
    }
}
