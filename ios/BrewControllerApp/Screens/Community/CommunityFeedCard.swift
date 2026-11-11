// The community feed: links owners suggested and a reviewer approved, with topic chips, "Suggest a
// link" (read the page, kind, title, topics, why) and your own suggestions (with their state and Delete).
import SwiftUI

struct CommunityFeedCard: View {
    let store: CommunityStore
    let onSuggest: () -> Void

    var body: some View {
        let palette = BrewTheme.shared.palette
        CommunityCard(title: "Community feed", identifier: "communityFeed") {
            Text("Videos, posts and guides about espresso that owners suggested and a reviewer approved.")
                .font(.footnote).foregroundStyle(palette.dim)
            Button("Suggest a link", action: onSuggest)
                .buttonStyle(.bordered)
                .accessibilityIdentifier("communitySuggest")
            let choices = store.topicChoices
            if !choices.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        topicChip("All", key: "")
                        ForEach(choices, id: \.key) { choice in topicChip(choice.label, key: choice.key) }
                    }
                }
                .accessibilityIdentifier("communityTopics")
            }
            if store.links == nil {
                CommunityEmptyNote(text: "The feed cannot be read from the controller.", identifier: "communityFeedEmpty")
            } else if store.shownLinks.isEmpty {
                CommunityEmptyNote(
                    text: "Nothing in the feed yet. It fills as owners suggest links and a reviewer approves them: suggest the first one.",
                    identifier: "communityFeedEmpty")
            } else {
                ForEach(store.shownLinks) { link in feedRow(link) }
            }
            let mine = store.links?.mine ?? []
            if !mine.isEmpty {
                Text("Your suggestions").font(.subheadline.weight(.semibold)).foregroundStyle(palette.text)
                    .padding(.top, 6)
                ForEach(mine) { suggestion in
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(suggestion.title).font(.subheadline.weight(.semibold)).foregroundStyle(palette.text)
                            Text((CommunityText.states[suggestion.state] ?? suggestion.state)
                                 + (suggestion.note.isEmpty ? "" : ": " + suggestion.note))
                                .font(.footnote).foregroundStyle(palette.dim)
                        }
                        Spacer(minLength: 0)
                        Button("Delete", role: .destructive) { Task { await store.delete(suggestion) } }
                            .buttonStyle(.bordered)
                            .font(.footnote)
                            .accessibilityIdentifier("communityDeleteSuggestion.\(suggestion.id)")
                    }
                    .accessibilityElement(children: .contain)
                    .accessibilityIdentifier("communitySuggestion.\(suggestion.id)")
                }
            }
        }
    }

    private func topicChip(_ title: String, key: String) -> some View {
        let palette = BrewTheme.shared.palette
        return Button(title) { store.topic = key }
            .buttonStyle(.bordered)
            .tint(store.topic == key ? palette.accent : palette.dim)
            .font(.footnote)
            .accessibilityAddTraits(store.topic == key ? .isSelected : [])
    }

    private func feedRow(_ link: CommunityLink) -> some View {
        let palette = BrewTheme.shared.palette
        let content = VStack(alignment: .leading, spacing: 4) {
            if let thumbnail = CommunityText.thumbnail(image: link.image, url: link.url) {
                AsyncImage(url: thumbnail) { phase in
                    if let image = phase.image {
                        image.resizable().aspectRatio(16 / 9, contentMode: .fill)
                    } else {
                        palette.track.opacity(0.5)
                    }
                }
                .frame(maxWidth: .infinity)
                .aspectRatio(16 / 9, contentMode: .fit)
                .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
            }
            Text(CommunityText.kindLabel(link.kind) + (link.site.isEmpty ? "" : " · " + link.site))
                .font(.caption).foregroundStyle(palette.dim)
            Text(link.title).font(.headline).foregroundStyle(palette.text).multilineTextAlignment(.leading)
            if !link.why.isEmpty { Text(link.why).font(.footnote).foregroundStyle(palette.text) }
            Text((link.topics.map { CommunityText.topicLabel($0) }.joined(separator: " · "))
                 + (link.added.isEmpty ? "" : " · added " + link.added))
                .font(.caption).foregroundStyle(palette.dim)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(palette.track.opacity(0.4), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
        return Group {
            if let url = URL(string: link.url) {
                Link(destination: url) { content }.buttonStyle(.plain)
            } else {
                content
            }
        }
        .accessibilityIdentifier("communityLink.\(link.id)")
    }
}

/// "Suggest a link" as a sheet.
struct CommunitySuggestView: View {
    let store: CommunityStore

    @Environment(\.dismiss) private var dismiss
    @State private var draft = CommunitySuggestionDraft()
    @State private var readStatus = ""
    @State private var reading = false
    @State private var sending = false
    @State private var problem = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("https://www.youtube.com/watch?v=...", text: $draft.url)
                        .textInputAutocapitalization(.never).keyboardType(.URL).autocorrectionDisabled()
                        .accessibilityIdentifier("communitySuggestURL")
                    Button(reading ? "Reading the page…" : "Read the page") { Task { await readPage() } }
                        .disabled(reading)
                        .accessibilityIdentifier("communitySuggestRead")
                    if !readStatus.isEmpty {
                        Text(readStatus).font(.footnote).foregroundStyle(BrewTheme.shared.palette.dim)
                    }
                } header: {
                    Text("Link")
                }
                Section {
                    TextField("Title", text: $draft.title)
                        .accessibilityIdentifier("communitySuggestTitle")
                        .onChange(of: draft.title) { _, text in if text.count > 79 { draft.title = String(text.prefix(79)) } }
                    Picker("What it is", selection: $draft.kind) {
                        ForEach(CommunityText.kinds, id: \.key) { kind in Text(kind.label).tag(kind.key) }
                    }
                    .accessibilityIdentifier("communitySuggestKind")
                }
                Section {
                    SubscriptionChipFlow(keys: CommunityText.topics.map { $0.key },
                                         words: Dictionary(uniqueKeysWithValues: CommunityText.topics.map { ($0.key, $0.label) }),
                                         selection: topicBinding)
                        .accessibilityIdentifier("communitySuggestTopics")
                } header: {
                    Text("What it is about (up to three)")
                }
                Section {
                    TextField("What someone gets from it, in a sentence.", text: $draft.why, axis: .vertical)
                        .lineLimit(2...4)
                        .accessibilityIdentifier("communitySuggestWhy")
                        .onChange(of: draft.why) { _, text in if text.count > 160 { draft.why = String(text.prefix(160)) } }
                } header: {
                    Text("Why it is worth it (optional)")
                } footer: {
                    Text("A reviewer checks it first; once approved it shows on every controller. It goes with this controller's key: no account, nothing personal.")
                }
                if !problem.isEmpty {
                    Section { Text(problem).foregroundStyle(BrewTheme.shared.palette.accent) }
                        .accessibilityIdentifier("communitySuggestProblem")
                }
            }
            .scrollContentBackground(.hidden)
            .background(BrewTheme.shared.palette.bg)
            .navigationTitle("Suggest a link")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Send for review") { Task { await send() } }
                        .disabled(sending)
                        .accessibilityIdentifier("communitySuggestSend")
                }
            }
        }
    }

    /// At most three topics ("Three topics at most").
    private var topicBinding: Binding<[String]> {
        Binding(get: { draft.topics }, set: { newValue in
            if newValue.count > 3 {
                problem = "Three topics at most"
            } else {
                problem = ""
                draft.topics = newValue
            }
        })
    }

    private func readPage() async {
        let url = draft.url.trimmingCharacters(in: .whitespaces)
        if let id = CommunityText.youtubeID(url) {
            draft.kind = url.contains("list=") && !url.contains("v=") ? "playlist" : "video"
            if draft.image.isEmpty { draft.image = "https://i.ytimg.com/vi/\(id)/hqdefault.jpg" }
        } else if url.contains("list=") {
            draft.kind = "playlist"
        }
        reading = true
        readStatus = "Reading the page..."
        defer { reading = false }
        guard let preview = await store.readPage(url) else {
            readStatus = store.notice?.text ?? "No answer from the page: type the title."
            return
        }
        if preview.state == "done" {
            if !preview.title.isEmpty && draft.title.isEmpty { draft.title = String(preview.title.prefix(79)) }
            if !preview.image.isEmpty && draft.image.isEmpty { draft.image = preview.image }
            readStatus = preview.site.isEmpty ? "Read. Check the title." : "Read from \(preview.site). Check the title."
        } else {
            readStatus = "Could not read it (" + (preview.problem.isEmpty ? "no answer" : preview.problem) + "): type the title."
        }
    }

    private func send() async {
        sending = true
        defer { sending = false }
        if await store.suggest(draft) {
            dismiss()
        } else {
            problem = store.notice?.text ?? ""
        }
    }
}
