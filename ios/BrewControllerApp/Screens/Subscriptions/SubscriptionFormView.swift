// The subscription form (web page: #subform): link and "Read the page", name, kind, status, every,
// next date, price, supplies, manage link, the shared-library details, sending it to the library, your
// review, the note and the card look. Shown as a sheet by SubscriptionsScreen.
import SwiftUI

struct SubscriptionFormTarget: Identifiable, Equatable {
    /// 0: a new subscription.
    let id: Int
}

struct SubscriptionFormView: View {
    let store: SubscriptionsStore
    let target: SubscriptionFormTarget

    @Environment(\.dismiss) private var dismiss
    @State private var draft: SubscriptionItem
    @State private var everyText: String
    @State private var everyUnit: Int
    @State private var hasNext: Bool
    @State private var nextDate: Date
    @State private var priceText: String
    @State private var stockText: String
    @State private var perShotText: String
    @State private var perDeliveryText: String
    @State private var shipsText: String
    @State private var cadenceText: String
    @State private var priceFromText: String
    @State private var cardColor: Color
    @State private var hasColor: Bool
    @State private var readStatus = ""
    @State private var reading = false
    @State private var confirmDelete = false
    @State private var confirmShare = false
    @State private var problem = ""

    private static let units: [(label: String, days: Double)] = [("weeks", 7), ("days", 1), ("months", 30.44)]

    init(store: SubscriptionsStore, target: SubscriptionFormTarget) {
        self.store = store
        self.target = target
        let item = store.list.subscription(id: target.id) ?? SubscriptionItem()
        _draft = State(initialValue: item)
        let days = item.everyDays
        var unit = 1
        if days > 0 && days % 7 == 0 { unit = 0 } else if days >= 28 && days <= 31 && days != 28 { unit = 2 }
        _everyUnit = State(initialValue: days == 0 ? 0 : unit)
        let divisor = SubscriptionFormView.units[days == 0 ? 0 : unit].days
        _everyText = State(initialValue: days == 0 ? "" : String(Int((Double(days) / divisor).rounded())))
        let date = BrewFormat.date(fromYMD: item.nextDate)
        _hasNext = State(initialValue: date != nil)
        _nextDate = State(initialValue: date ?? Date())
        _priceText = State(initialValue: item.priceCents > 0 ? BrewFormat.decimal(Double(item.priceCents) / 100, 2) : "")
        _stockText = State(initialValue: item.stock > 0 ? String(item.stock) : "")
        _perShotText = State(initialValue: item.perShot > 0 ? String(item.perShot) : "")
        _perDeliveryText = State(initialValue: item.perDelivery > 0 ? String(item.perDelivery) : "")
        _shipsText = State(initialValue: item.shipsTo.split(separator: ",").joined(separator: ", "))
        _cadenceText = State(initialValue: item.cadences.map { BrewFormat.decimal(Double($0) / 7, 1) }
            .map { $0.hasSuffix(".0") ? String($0.dropLast(2)) : $0 }.joined(separator: ", "))
        _priceFromText = State(initialValue: item.priceFromCents > 0 ? BrewFormat.decimal(Double(item.priceFromCents) / 100, 2) : "")
        let parsed = SubscriptionColors.color(hex: item.color)
        _cardColor = State(initialValue: parsed ?? Color(.sRGB, red: 0.36, green: 0.39, blue: 0.44, opacity: 1))
        _hasColor = State(initialValue: parsed != nil)
    }

    private var isNew: Bool { target.id == 0 }
    private var saved: SubscriptionItem? { store.list.subscription(id: target.id) }

    var body: some View {
        NavigationStack {
            Form {
                linkSection
                basicsSection
                if draft.kind == "supplies" { suppliesSection }
                Section {
                    TextField("https://", text: $draft.manageUrl)
                        .textInputAutocapitalization(.never).keyboardType(.URL).autocorrectionDisabled()
                        .accessibilityIdentifier("subscriptionFormManage")
                } header: {
                    Text("Where you manage it (the shop's subscription page)")
                }
                libraryDetailsSection
                if let saved, !isNew { shareSection(saved) }
                if !isNew {
                    Section("Your review") {
                        SubscriptionReviewEditor(store: store, subscription: target.id, shareRatings: store.list.shareRatings,
                                                 shared: saved?.share == "published")
                    }
                }
                Section("Note") {
                    TextField("e.g. 2 bags, whole bean", text: $draft.note)
                        .accessibilityIdentifier("subscriptionFormNote")
                        .onChange(of: draft.note) { _, text in if text.count > 95 { draft.note = String(text.prefix(95)) } }
                }
                lookSection
                if !problem.isEmpty {
                    Section { Text(problem).foregroundStyle(BrewTheme.shared.palette.accent) }
                        .accessibilityIdentifier("subscriptionFormProblem")
                }
                if !isNew {
                    Section {
                        Button("Delete", role: .destructive) { confirmDelete = true }
                            .accessibilityIdentifier("subscriptionFormDelete")
                    }
                }
            }
            .scrollContentBackground(.hidden)
            .background(BrewTheme.shared.palette.bg)
            .navigationTitle(isNew ? "Add a subscription" : "Edit " + (draft.name.isEmpty ? "subscription" : draft.name))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }.accessibilityIdentifier("subscriptionFormCancel")
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Save") { Task { await save() } }.accessibilityIdentifier("subscriptionFormSave")
                }
            }
            .alert("Delete \"\(draft.name.isEmpty ? "this subscription" : draft.name)\"?", isPresented: $confirmDelete) {
                Button("Delete", role: .destructive) {
                    Task { if let saved, await store.delete(saved) { dismiss() } }
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Bags it brought stay on the shelf.")
            }
            .alert("Send \"\(draft.name)\" to the shared library?", isPresented: $confirmShare) {
                Button("Send") { Task { if let saved { _ = await store.share(saved) } } }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Its name, links, kind, where it ships, deliveries offered, typical price, style and sourcing claims are sent. Your own price, dates, stock and notes are not.")
            }
        }
        .accessibilityIdentifier("subscriptionForm")
    }

    // MARK: Sections

    private var linkSection: some View {
        Section {
            TextField("Link to the shop or the subscription", text: $draft.url)
                .textInputAutocapitalization(.never).keyboardType(.URL).autocorrectionDisabled()
                .accessibilityIdentifier("subscriptionFormURL")
            Button(reading ? "Reading the page…" : "Read the page") { Task { await readPage() } }
                .disabled(reading)
                .accessibilityIdentifier("subscriptionFormRead")
            if !readStatus.isEmpty {
                Text(readStatus).font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
                    .accessibilityIdentifier("subscriptionFormReadStatus")
            }
        }
    }

    private var basicsSection: some View {
        Section {
            TextField("Name", text: $draft.name)
                .accessibilityIdentifier("subscriptionFormName")
                .onChange(of: draft.name) { _, text in if text.count > 47 { draft.name = String(text.prefix(47)) } }
            Picker("What it is", selection: $draft.kind) {
                Text("Coffee").tag("coffee")
                Text("Supplies (filters...)").tag("supplies")
                Text("Other").tag("other")
            }
            .accessibilityIdentifier("subscriptionFormKind")
            Picker("Status", selection: $draft.status) {
                Text("Active").tag("active")
                Text("Paused").tag("paused")
                Text("Cancelled").tag("cancelled")
            }
            .accessibilityIdentifier("subscriptionFormStatus")
            HStack {
                Text("Every")
                Spacer()
                TextField("0", text: $everyText)
                    .keyboardType(.numberPad).multilineTextAlignment(.trailing).frame(width: 60)
                    .accessibilityIdentifier("subscriptionFormEvery")
                Picker("Unit", selection: $everyUnit) {
                    ForEach(0..<SubscriptionFormView.units.count, id: \.self) { index in
                        Text(SubscriptionFormView.units[index].label).tag(index)
                    }
                }
                .labelsHidden()
            }
            Toggle("Next charge or delivery is known", isOn: $hasNext)
                .accessibilityIdentifier("subscriptionFormHasNext")
            if hasNext {
                DatePicker("Next charge or delivery", selection: $nextDate, displayedComponents: .date)
                    .accessibilityIdentifier("subscriptionFormNext")
            }
            HStack {
                Text("Price each time ($)")
                Spacer()
                TextField("0.00", text: $priceText)
                    .keyboardType(.decimalPad).multilineTextAlignment(.trailing).frame(width: 90)
                    .accessibilityIdentifier("subscriptionFormPrice")
            }
        }
    }

    private var suppliesSection: some View {
        Section("Supplies") {
            TextField("Item, e.g. paper filters", text: $draft.item)
                .accessibilityIdentifier("subscriptionFormItem")
                .onChange(of: draft.item) { _, text in if text.count > 39 { draft.item = String(text.prefix(39)) } }
            numberRow("In stock now", text: $stockText, id: "subscriptionFormStock")
            numberRow("Used per shot", text: $perShotText, id: "subscriptionFormPerShot")
            numberRow("Each delivery brings", text: $perDeliveryText, id: "subscriptionFormPerDelivery")
        }
    }

    private func numberRow(_ title: String, text: Binding<String>, id: String) -> some View {
        HStack {
            Text(title)
            Spacer()
            TextField("0", text: text)
                .keyboardType(.numberPad).multilineTextAlignment(.trailing).frame(width: 80)
                .accessibilityIdentifier(id)
        }
    }

    private var libraryDetailsSection: some View {
        Section {
            DisclosureGroup("For the shared library: what it offers, where it ships, how it sources") {
                TextField("Ships to (country codes, e.g. US, CA)", text: $shipsText)
                    .textInputAutocapitalization(.characters).autocorrectionDisabled()
                    .accessibilityIdentifier("subscriptionFormShips")
                TextField("Deliveries offered (every N weeks, e.g. 1, 2, 4)", text: $cadenceText)
                    .accessibilityIdentifier("subscriptionFormCadences")
                HStack {
                    Text("Typical price each delivery")
                    Spacer()
                    TextField("0.00", text: $priceFromText).keyboardType(.decimalPad)
                        .multilineTextAlignment(.trailing).frame(width: 80)
                }
                TextField("Currency, e.g. USD", text: $draft.currency)
                    .textInputAutocapitalization(.characters).autocorrectionDisabled()
                    .onChange(of: draft.currency) { _, text in if text.count > 3 { draft.currency = String(text.prefix(3)) } }
                chips("Style", keys: SubscriptionWording.styleKeys, words: SubscriptionWording.style, selection: $draft.style)
                chips("Sourcing it claims", keys: SubscriptionWording.sourcingKeys, words: SubscriptionWording.sourcing,
                      selection: $draft.sourcing)
                Text("Claims are checked against the shop's site by the library's reviewer before it is listed. Your own price, dates, stock and notes are never sent.")
                    .font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
            }
            .accessibilityIdentifier("subscriptionFormShared")
        }
    }

    private func chips(_ title: String, keys: [String], words: [String: String], selection: Binding<[String]>) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.subheadline.weight(.semibold))
            SubscriptionChipFlow(keys: keys, words: words, selection: selection)
        }
    }

    private func shareSection(_ saved: SubscriptionItem) -> some View {
        let palette = BrewTheme.shared.palette
        let state = saved.share
        let fromLibrary = state == "published" && !saved.libraryId.isEmpty
        let text: String
        if fromLibrary {
            text = "Listed as \(saved.libraryId)" + (saved.shareChanged ? ". You have changed it since: send the change for review." : ".")
        } else if state == "declined" || state == "refused" {
            text = saved.reviewNote
        } else if !store.list.sharing {
            text = "Sharing is off (Setup, Shared library)."
        } else if !saved.canShare {
            text = "It needs a name and an https link to be shared."
        } else if state == "local" {
            text = "Send it so other people can add it in one tap. A reviewer checks it first."
        } else {
            text = ""
        }
        let canSend = store.list.sharing && saved.canShare
            && (state == "local" || state == "declined" || state == "refused" || saved.shareChanged)
        return Section {
            Text(SubscriptionWording.share[state] ?? state).font(.subheadline.weight(.semibold))
                .accessibilityIdentifier("subscriptionFormShareState")
            if !text.isEmpty { Text(text).font(.footnote).foregroundStyle(palette.dim) }
            if canSend {
                Button("Send to the shared library") { confirmShare = true }
                    .accessibilityIdentifier("subscriptionFormShare")
            }
        }
    }

    private var lookSection: some View {
        Section {
            DisclosureGroup("Card look") {
                TextField("Picture (link)", text: $draft.image).textInputAutocapitalization(.never)
                    .keyboardType(.URL).autocorrectionDisabled()
                TextField("Icon (link)", text: $draft.icon).textInputAutocapitalization(.never)
                    .keyboardType(.URL).autocorrectionDisabled()
                Toggle("Use a card colour", isOn: $hasColor)
                if hasColor { ColorPicker("Colour", selection: $cardColor, supportsOpacity: false) }
            }
        }
    }

    // MARK: Actions

    private func readPage() async {
        let url = draft.url.trimmingCharacters(in: .whitespaces)
        reading = true
        readStatus = "Reading the page…"
        defer { reading = false }
        guard let preview = await store.readPage(url) else {
            readStatus = store.notice?.text ?? ""
            return
        }
        switch preview.state {
        case "failed":
            readStatus = (preview.problem.isEmpty ? "The page could not be read" : preview.problem)
                + ". Fill in the name; the card uses its initial."
            if draft.name.isEmpty, let host = URL(string: url)?.host {
                draft.name = host.hasPrefix("www.") ? String(host.dropFirst(4)) : host
            }
        case "done":
            draft.pageTitle = preview.title
            draft.site = preview.site
            draft.pageDescription = preview.pageDescription
            draft.platform = preview.platform
            if draft.name.isEmpty { draft.name = preview.site.isEmpty ? preview.title : preview.site }
            if draft.image.isEmpty { draft.image = preview.image }
            if draft.icon.isEmpty { draft.icon = preview.icon }
            if draft.manageUrl.isEmpty { draft.manageUrl = preview.manageUrl }
            if !hasColor, let color = SubscriptionColors.color(hex: preview.color),
               !["#fff", "#ffffff", "#000", "#000000"].contains(preview.color.lowercased()) {
                cardColor = color
                hasColor = true
            }
            let platform = SubscriptionWording.platform[preview.platform].map { "subscriptions by " + $0 }
            readStatus = "Read: " + [preview.title, platform].compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: " · ")
                + (preview.manageUrl.isEmpty ? "" : ". Check the Manage link below.")
        default:
            readStatus = "Still reading: try again in a moment."
        }
    }

    private func save() async {
        var item = draft
        item.url = item.url.trimmingCharacters(in: .whitespaces)
        item.name = item.name.trimmingCharacters(in: .whitespaces)
        item.manageUrl = item.manageUrl.trimmingCharacters(in: .whitespaces)
        if item.name.isEmpty && item.url.isEmpty {
            problem = "Give it a name or a link"
            return
        }
        let lower = item.manageUrl.lowercased()
        if !lower.isEmpty && !lower.hasPrefix("http://") && !lower.hasPrefix("https://") {
            problem = "The manage link must start https://"
            return
        }
        problem = ""
        let every = Double(everyText) ?? 0
        item.everyDays = min(366, Int((every * SubscriptionFormView.units[everyUnit].days).rounded()))
        item.nextDate = hasNext ? SubscriptionWording.ymd(nextDate) : 0
        item.priceCents = Int(((Double(priceText.replacingOccurrences(of: ",", with: ".")) ?? 0) * 100).rounded())
        item.stock = max(0, Int(stockText) ?? 0)
        item.perShot = item.kind == "supplies" ? max(0, Int(perShotText) ?? 0) : 0
        item.perDelivery = max(0, Int(perDeliveryText) ?? 0)
        item.shipsTo = shipsText.uppercased().split(whereSeparator: { !$0.isLetter }).filter { $0.count == 2 }
            .joined(separator: ",")
        item.cadences = Array(cadenceText.split(whereSeparator: { !($0.isNumber || $0 == ".") })
            .compactMap { Double($0) }.map { Int(($0 * 7).rounded()) }.filter { $0 > 0 && $0 <= 366 }.prefix(6))
        item.priceFromCents = Int(((Double(priceFromText.replacingOccurrences(of: ",", with: ".")) ?? 0) * 100).rounded())
        item.color = hasColor ? SubscriptionColors.hex(cardColor) : ""
        if await store.save(item) != nil {
            store.notice = SubscriptionNotice(text: "Subscription saved", good: true)
            dismiss()
        } else {
            problem = store.notice?.text ?? "Could not save."
        }
    }
}

/// Chips that wrap, each toggling one key.
struct SubscriptionChipFlow: View {
    let keys: [String]
    let words: [String: String]
    @Binding var selection: [String]

    var body: some View {
        let palette = BrewTheme.shared.palette
        SubscriptionFlowLayout(spacing: 6) {
            ForEach(keys, id: \.self) { key in
                let on = selection.contains(key)
                Button {
                    if on { selection.removeAll { $0 == key } } else { selection.append(key) }
                } label: {
                    Text(words[key] ?? key)
                        .font(.footnote)
                        .padding(.horizontal, 10).padding(.vertical, 5)
                        .background(on ? palette.accent.opacity(0.85) : palette.track.opacity(0.6), in: Capsule())
                        .foregroundStyle(on ? Color.white : palette.text)
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(on ? .isSelected : [])
            }
        }
    }
}

/// A simple wrapping layout (lines of views that break when they run out of width).
struct SubscriptionFlowLayout: Layout {
    var spacing: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        arrange(width: proposal.width ?? 320, subviews: subviews).size
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let result = arrange(width: bounds.width, subviews: subviews)
        for (index, origin) in result.origins.enumerated() {
            subviews[index].place(at: CGPoint(x: bounds.minX + origin.x, y: bounds.minY + origin.y), proposal: .unspecified)
        }
    }

    private func arrange(width: CGFloat, subviews: Subviews) -> (size: CGSize, origins: [CGPoint]) {
        var origins: [CGPoint] = []
        var x: CGFloat = 0, y: CGFloat = 0, rowHeight: CGFloat = 0, maxX: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x > 0 && x + size.width > width {
                x = 0
                y += rowHeight + spacing
                rowHeight = 0
            }
            origins.append(CGPoint(x: x, y: y))
            x += size.width + spacing
            rowHeight = max(rowHeight, size.height)
            maxX = max(maxX, x - spacing)
        }
        return (CGSize(width: maxX, height: y + rowHeight), origins)
    }
}
