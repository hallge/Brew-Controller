// One coffee (W: #coffees, the form and the cards under it): the form, the bag in use, new bag, bag
// finished, delete, the bags of this coffee, sharing, how you liked it, the recipes to brew it with, and
// what the community settled on. `coffeeId` 0 is a new coffee.
import SwiftUI

struct CoffeeDetailView: View {
    let store: CoffeeStore
    let coffeeId: Int
    /// A form to start from (a visualizer.coffee result); nil: an empty form or the stored coffee.
    let draftStart: CoffeeDraft?
    /// Called when a new coffee has been saved or the form is cancelled (it closes the sheet).
    let onFinished: (() -> Void)?

    @Environment(\.dismiss) private var dismiss

    @State private var draft = CoffeeDraft()
    @State private var loadedFor: Int?
    @State private var saving = false
    /// The altitude field's text: it goes into `draft` on every keystroke, not when the edit ends.
    @State private var altitudeText = ""

    @State private var showNewBag = false
    @State private var bagRoastDate = 0
    @State private var bagOpenedDate = 0

    @State private var confirmFinish = false
    @State private var confirmDelete = false
    @State private var confirmDuplicate = false
    @State private var confirmSameRoast = false
    @State private var bagToForget: Int?

    private var coffee: CoffeeModel? { store.coffee(id: coffeeId) }
    private var isNew: Bool { coffeeId == 0 }

    var body: some View {
        Form {
            coffeeSection
            bagInUseSection
            originSection
            actionsSection

            if !isNew, let coffee {
                if showNewBag { newBagSection(coffee) }
                bagsSection(coffee)
                shareSection(coffee)
                CoffeeRatingSection(store: store, coffee: coffee)
                brewWithSection(coffee)
                CoffeeCommunitySection(store: store, coffee: coffee)
            }
        }
        .accessibilityIdentifier("coffeeForm")
        .coffeeThemed()
        .navigationTitle(isNew ? "New coffee" : (coffee?.name ?? "Coffee"))
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            if isNew {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { finish() }
                        .accessibilityIdentifier("coffeeCancel")
                }
            }
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("Done") { Task { await endEditing() } }
            }
        }
        .onAppear(perform: loadDraft)
        .onChange(of: altitudeText) { _, text in draft.altitudeM = Int(text.filter(\.isNumber)) ?? 0 }
        .onChange(of: draft.altitudeM) { _, metres in
            if (Int(altitudeText) ?? 0) != metres { altitudeText = altitudeShown(metres) }
        }
        .onChange(of: store.library) { _, _ in loadDraft() }
        .confirmationDialog("Is the bag of \"\(coffee?.name ?? "")\" finished?", isPresented: $confirmFinish,
                            titleVisibility: .visible) {
            Button("Bag finished") {
                if let coffee { Task { await store.finish(coffee) } }
            }
        } message: {
            Text("It moves to Finished bags with everything kept, and leaves the controller until you start a new bag.")
        }
        .confirmationDialog("Delete \"\(coffee?.name ?? "")\" from the shelf?", isPresented: $confirmDelete,
                            titleVisibility: .visible) {
            Button("Delete", role: .destructive) { Task { await delete() } }
        } message: {
            Text("Its shots stay in the history.")
        }
        .confirmationDialog("\"\(draft.name)\" is already on the shelf. Add it again?", isPresented: $confirmDuplicate,
                            titleVisibility: .visible) {
            Button("Add it again") { Task { await save(force: true) } }
        }
        .confirmationDialog("The bag in use has this roast date. Start another bag anyway?",
                            isPresented: $confirmSameRoast, titleVisibility: .visible) {
            Button("Start another bag") { Task { await startBag(force: true) } }
        }
        .confirmationDialog("Forget this bag?", isPresented: Binding(
            get: { bagToForget != nil }, set: { if !$0 { bagToForget = nil } }
        ), titleVisibility: .visible) {
            Button("Forget", role: .destructive) {
                if let bag = bagToForget { Task { await store.forgetBag(bag) } }
                bagToForget = nil
            }
        } message: {
            Text("Its shots stay in the history.")
        }
        .coffeeNotices(store)
    }

    // MARK: Form sections

    private var coffeeSection: some View {
        Section("The coffee") {
            CoffeeTextRow(title: "Name", text: $draft.name, limit: 23, placeholder: "e.g. Idjwi Island", identifier: "coffeeName")
            CoffeeTextRow(title: "Roaster", text: $draft.roaster, limit: 23, placeholder: "e.g. Portrait", identifier: "coffeeRoaster")
            Picker("Roast level", selection: $draft.roastLevel) {
                ForEach(0..<CoffeeRoast.names.count, id: \.self) { level in
                    Text(CoffeeRoast.label(level)).tag(level)
                }
            }
            .accessibilityIdentifier("coffeeRoastLevel")
            CoffeeTextRow(title: "Tasting notes", text: $draft.notes, limit: 47, placeholder: "e.g. blueberry, cocoa",
                          identifier: "coffeeNotes")
        }
    }

    private var bagInUseSection: some View {
        Section {
            CoffeeDateRow(title: "Roast date printed on the bag", ymd: $draft.roastDate, identifier: "coffeeRoastDate")
            CoffeeDateRow(title: "Put in the freezer", ymd: $draft.frozenDate, identifier: "coffeeFrozenDate")
            CoffeeDateRow(title: "Taken out", ymd: $draft.thawDate, identifier: "coffeeThawDate")
        } header: {
            Text("The bag in use")
        } footer: {
            Text("The dates are those of the bag in use; correcting them corrects that bag. Days in the freezer are not counted as age: enter the day the bag went in and the day it came out (leave the second empty if it is still frozen).")
        }
    }

    private var originSection: some View {
        Section("Where it is from (optional)") {
            CoffeeTextRow(title: "Origin (country)", text: $draft.origin, limit: 23, placeholder: "", identifier: "coffeeOrigin")
            CoffeeTextRow(title: "Region", text: $draft.region, limit: 31, placeholder: "", identifier: "coffeeRegion")
            CoffeeTextRow(title: "Producer or washing station", text: $draft.producer, limit: 31, placeholder: "",
                          identifier: "coffeeProducer")
            CoffeeTextRow(title: "Varietal", text: $draft.varietal, limit: 31, placeholder: "", identifier: "coffeeVarietal")
            CoffeeTextRow(title: "Process", text: $draft.process, limit: 15, placeholder: "washed, natural...",
                          identifier: "coffeeProcess")
            HStack {
                Text("Altitude (m)")
                Spacer()
                TextField("", text: $altitudeText)
                    .keyboardType(.numberPad)
                    .multilineTextAlignment(.trailing)
                    .frame(maxWidth: 90)
                    .accessibilityIdentifier("coffeeAltitude")
            }
        }
    }

    /// Altitude 0 shows as an empty field.
    private func altitudeShown(_ metres: Int) -> String { metres == 0 ? "" : String(metres) }

    private var actionsSection: some View {
        Section {
            Button("Save") { Task { await save(force: false) } }
                .disabled(saving)
                .accessibilityIdentifier("coffeeSave")
            if !isNew, let coffee {
                Button(coffee.finished != 0 ? "New bag: back on the shelf..." : "New bag...") {
                    bagRoastDate = 0
                    bagOpenedDate = 0
                    showNewBag = true
                }
                .accessibilityIdentifier("coffeeNewBag")
                if coffee.finished == 0 && coffee.bagId != 0 {
                    Button("Bag finished") { confirmFinish = true }
                        .accessibilityIdentifier("coffeeFinish")
                }
                Button("Delete", role: .destructive) { confirmDelete = true }
                    .accessibilityIdentifier("coffeeDelete")
            }
        } footer: {
            if let coffee, !isNew {
                if store.waitingBag(coffee) != nil && (coffee.finished != 0 || coffee.bagId == 0) {
                    Text("Waiting unopened: open it below when you start it.")
                } else if coffee.finished != 0 {
                    Text("Bag finished \(CoffeeDates.text(coffee.finished)). Off the controller until a new bag is started.")
                } else {
                    Text("A new bag keeps the coffee, recipes and grind setting, and starts a fresh run for grind advice. Earlier bags are kept below: put one back in use if you go back to it.")
                }
            }
        }
    }

    // MARK: New bag and the bags

    private func newBagSection(_ coffee: CoffeeModel) -> some View {
        Section {
            CoffeeDateRow(title: "Roast date printed on the new bag", ymd: $bagRoastDate, identifier: "coffeeBagRoastDate")
            CoffeeDateRow(title: "Opened (optional)", ymd: $bagOpenedDate, identifier: "coffeeBagOpened")
            Button("Start this bag") { Task { await startBag(force: false) } }
                .accessibilityIdentifier("coffeeBagStart")
            Button("Cancel", role: .cancel) { showNewBag = false }
                .accessibilityIdentifier("coffeeBagCancel")
        } header: {
            Text("New bag")
        }
    }

    @ViewBuilder
    private func bagsSection(_ coffee: CoffeeModel) -> some View {
        let bags = store.bags(of: coffee.id)
        if !bags.isEmpty {
            Section("Bags of \(coffee.name)") {
                ForEach(bags) { bag in
                    CoffeeBagRow(
                        bag: bag,
                        onUse: { Task { await store.useBag(coffee: coffee.id, bag: bag.id) } },
                        onOpen: { Task { await store.openBag(bag.id) } },
                        onArrived: { Task { await store.bagArrived(bag.id) } },
                        onForget: { bagToForget = bag.id })
                        .accessibilityIdentifier("coffeeBag-\(bag.id)")
                }
            }
        }
    }

    // MARK: Sharing

    @ViewBuilder
    private func shareSection(_ coffee: CoffeeModel) -> some View {
        let state = coffee.share
        let toLibrary = store.share?.toLibrary ?? true
        let canSend = store.shareEnabled && (state == "local" || state == "refused" || coffee.shareChanged)
        Section {
            HStack {
                Text(state == "sent" && !toLibrary ? "not sent" : CoffeeShareText.state(state))
                    .font(.caption.weight(.semibold))
                    .padding(.horizontal, 8).padding(.vertical, 3)
                    .background(Color.accentColor.opacity(0.18), in: Capsule())
                    .accessibilityIdentifier("coffeeSharePill")
                Text(shareText(coffee, toLibrary: toLibrary)).font(.footnote).foregroundStyle(.secondary)
            }
            if canSend {
                Button(state == "local" ? "Send to the shared library"
                       : state == "refused" ? "Send again" : "Send the new version") {
                    Task { await store.send(coffee) }
                }
                .accessibilityIdentifier("coffeeSend")
            }
        } header: {
            Text("Share this coffee")
        }
    }

    private func shareText(_ coffee: CoffeeModel, toLibrary: Bool) -> String {
        let note = coffee.reviewNote
        var text: String
        switch coffee.share {
        case "local":
            text = store.shareEnabled ? "Not sent to the shared library yet."
                : "Sharing is switched off (Setup), so this coffee stays here."
        case "queued": text = "Signed and waiting in the outbox."
        case "sent":
            text = toLibrary ? "Sent to the library. Its intake runs within a minute or two and puts it up for review."
                : "Written to this device only. It has not gone to the library."
        case "review": text = "The library has it. A reviewer has not decided yet."
        case "published":
            text = "Approved by a reviewer" + (coffee.libraryId.isEmpty ? "" : ": in the library as " + coffee.libraryId) + "."
        case "declined":
            text = "A reviewer did not accept it" + (note.isEmpty ? "" : ": \"" + note + "\"") + ". What you typed is unchanged."
        case "refused":
            text = "The library's checks turned this record away" + (note.isEmpty ? "" : ": \"" + note + "\"")
                + ". Correct it if needed, then send it again."
        default: text = ""
        }
        if coffee.share != "declined" && coffee.share != "refused" && !note.isEmpty { text += " Reviewer: \"" + note + "\"" }
        if coffee.shareChanged { text += " You have changed it since that version was sent." }
        return text
    }

    // MARK: Brew it with

    @ViewBuilder
    private func brewWithSection(_ coffee: CoffeeModel) -> some View {
        let presets = store.library?.presets ?? []
        Section {
            ForEach(store.recipes) { recipe in
                let preset = presets.first { $0.coffeeId == coffee.id && $0.recipeId == recipe.id }
                VStack(alignment: .leading, spacing: 2) {
                    Toggle(isOn: Binding(
                        get: { preset != nil },
                        set: { on in Task { await store.setBrewWith(coffee: coffee.id, recipe: recipe.id, on: on) } }
                    )) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(recipe.name).font(.headline)
                            Text(recipeLine(recipe)).font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    .disabled(coffee.finished != 0)
                    .accessibilityIdentifier("coffeeBrewWith-\(recipe.id)")
                    if let preset {
                        Text("grinder " + (preset.grindUm > 0 ? store.dial.settingUnit(preset.grindUm) : "not set yet"))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            }
            NavigationLink {
                RecipesScreen()
            } label: {
                Text("Recipes")
            }
            .accessibilityIdentifier("coffeeRecipesLinkDetail")
        } header: {
            Text("Brew it with")
        } footer: {
            Text("Each ticked line is one entry on the controller (\(presets.count) of \(store.library?.maxPresets ?? 0) used). The grind setting and the learned stop offset belong to the pair." + (coffee.finished != 0 ? " Bag finished: start a new bag first." : ""))
        }
    }

    private func recipeLine(_ recipe: RecipeDefModel) -> String {
        var line = BrewFormat.grams(recipe.doseG) + " g in, " + BrewFormat.grams(recipe.targetWeightG) + " g out, "
            + BrewFormat.decimal(recipe.targetTimeS, 0) + " s"
        if recipe.bloomPauseS > 0 { line += ", bloom " + BrewFormat.decimal(recipe.bloomPauseS, 0) + " s" }
        return line
    }

    // MARK: Actions

    private func loadDraft() {
        if let draftStart, loadedFor == nil {
            draft = draftStart
            loadedFor = coffeeId
            return
        }
        guard loadedFor != coffeeId else { return }
        if isNew {
            loadedFor = 0
            return
        }
        guard let coffee else { return }
        draft = CoffeeDraft(coffee)
        loadedFor = coffeeId
    }

    private func endEditing() async {
        UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
        try? await Task.sleep(for: .milliseconds(60))
    }

    private func finish() {
        if let onFinished { onFinished() } else { dismiss() }
    }

    private func save(force: Bool) async {
        saving = true
        defer { saving = false }
        await endEditing()
        if isNew, !force, store.isDuplicate(draft.clipped()), !draft.clipped().name.isEmpty {
            confirmDuplicate = true
            return
        }
        guard await store.save(draft, id: coffeeId) != nil else { return }
        if isNew { finish() } else { draft = draft.clipped() }
    }

    private func startBag(force: Bool) async {
        guard let coffee else {
            store.errorText = "Save the coffee first"
            return
        }
        await endEditing()
        guard bagRoastDate != 0 else {
            store.errorText = "Enter the roast date printed on the bag"
            return
        }
        guard bagRoastDate <= CoffeeDates.today() else {
            store.errorText = "That date has not come yet"
            return
        }
        if coffee.roastDate == bagRoastDate && !force {
            confirmSameRoast = true
            return
        }
        if await store.startBag(coffee: coffee.id, roastDate: bagRoastDate, openedDate: bagOpenedDate) {
            showNewBag = false
            if let fresh = store.coffee(id: coffeeId) { draft = CoffeeDraft(fresh) }
        }
    }

    private func delete() async {
        guard let coffee else { return }
        if await store.delete(coffee) { dismiss() }
    }
}

// MARK: - Rows

/// A text field with its title on the left and the controller's length limit.
struct CoffeeTextRow: View {
    let title: String
    @Binding var text: String
    let limit: Int
    let placeholder: String
    let identifier: String

    var body: some View {
        HStack {
            Text(title)
            Spacer(minLength: 12)
            TextField(placeholder, text: $text)
                .multilineTextAlignment(.trailing)
                .autocorrectionDisabled()
                .onChange(of: text) { _, value in
                    if value.count > limit { text = String(value.prefix(limit)) }
                }
                .accessibilityIdentifier(identifier)
        }
    }
}

/// A date that may be empty: "Set" puts today in, a picker changes it, "Clear" empties it.
struct CoffeeDateRow: View {
    let title: String
    @Binding var ymd: Int
    let identifier: String

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            if ymd != 0 {
                DatePicker("", selection: dateBinding, displayedComponents: .date)
                    .labelsHidden()
                    .accessibilityIdentifier(identifier)
                Button {
                    ymd = 0
                } label: {
                    Image(systemName: "xmark.circle.fill").foregroundStyle(.secondary)
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("Clear " + title)
                .accessibilityIdentifier(identifier + "Clear")
            } else {
                Button("Set") { ymd = CoffeeDates.today() }
                    .buttonStyle(.borderless)
                    .accessibilityIdentifier(identifier)
            }
        }
    }

    private var dateBinding: Binding<Date> {
        Binding(get: { CoffeeDates.date(ymd) ?? Date() }, set: { ymd = CoffeeDates.ymd($0) })
    }
}

/// One bag: what is known about it and the buttons its state allows (WebPage.h `drawBags`).
private struct CoffeeBagRow: View {
    let bag: BagModel
    let onUse: () -> Void
    let onOpen: () -> Void
    let onArrived: () -> Void
    let onForget: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 6) {
                Text(description).font(.subheadline)
                if let pill { Text(pill).font(.caption2.weight(.semibold)).padding(.horizontal, 6).padding(.vertical, 2)
                    .background(Color.accentColor.opacity(0.18), in: Capsule()) }
            }
            HStack {
                if bag.onTheWay {
                    Button("It arrived", action: onArrived)
                    Button("Forget", role: .destructive, action: onForget)
                } else if bag.unopened {
                    Button("Open this bag", action: onOpen)
                    Button("Forget", role: .destructive, action: onForget)
                } else if !bag.current {
                    Button("Use this bag", action: onUse)
                    Button("Forget", role: .destructive, action: onForget)
                }
            }
            .buttonStyle(.borderless)
            .font(.subheadline)
        }
    }

    private var pill: String? {
        if bag.onTheWay { return "on its way" }
        if bag.unopened { return "unopened" }
        return bag.current ? "in use" : nil
    }

    private var description: String {
        if bag.onTheWay {
            var text = bag.expectedDate != 0 ? "expected " + CoffeeDates.text(bag.expectedDate) : "not come yet"
            if bag.weightG > 0 { text += ", \(bag.weightG) g" }
            return text
        }
        var parts: [String] = []
        parts.append(bag.roastDate != 0 ? "roasted " + CoffeeDates.text(bag.roastDate) : "roast date not entered")
        if bag.openedDate != 0 { parts.append("opened " + CoffeeDates.text(bag.openedDate)) }
        if let days = frozenDays {
            parts.append("\(days) days frozen" + (bag.frozenDate != 0 && bag.thawDate == 0 ? " (still there)" : ""))
        }
        if bag.weightG > 0 { parts.append("\(bag.weightG) g") }
        return parts.joined(separator: ", ")
    }

    /// Days in the freezer: from the day it went in to the day it came out (or today).
    private var frozenDays: Int? {
        guard let start = CoffeeDates.date(bag.frozenDate) else { return nil }
        let end = CoffeeDates.date(bag.thawDate) ?? Date()
        let days = Calendar(identifier: .gregorian).dateComponents([.day], from: start, to: end).day ?? 0
        return days > 0 ? days : nil
    }
}
