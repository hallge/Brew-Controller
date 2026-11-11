// Community (web page: #community): the feed and link suggestions, the Reddit share, trending recipes,
// what other owners settled on for what you are brewing, the shared grinders and what you share. Pushed
// by the More tab inside its NavigationStack.
import SwiftUI

struct CommunityScreen: View {
    /// A shot to share to Reddit: the screen scrolls to that card with this shot chosen.
    let redditShotID: Int?

    @State private var store = CommunityStore()
    @State private var showSuggest = false
    @State private var recipeDraft: DiscoverRecipeDraft?

    init(redditShotID: Int? = nil) {
        self.redditShotID = redditShotID
    }

    var body: some View {
        let palette = BrewTheme.shared.palette
        ScrollViewReader { reader in
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
                        .accessibilityIdentifier("communityNotice")
                }
                if store.phase == .loading {
                    ProgressView("Loading").frame(maxWidth: .infinity, minHeight: 140)
                        .accessibilityIdentifier("communityLoading")
                } else {
                    CommunityFeedCard(store: store) { showSuggest = true }
                    CommunityRedditCard(store: store, initialShotID: redditShotID)
                        .id("communityRedditCard")
                    CommunityTrendingCard(store: store) { draft in recipeDraft = draft }
                    CommunityNowCard(store: store)
                    CommunityCard(title: "Coffees and recipes in the shared library", identifier: "communityLibraryPointer") {
                        Text("Browse them, add them, and review what other owners sent on Discover.")
                            .font(.subheadline).foregroundStyle(palette.dim)
                        NavigationLink("Discover") { DiscoverScreen() }
                            .accessibilityIdentifier("communityLinkDiscover")
                    }
                    CommunityGrindersCard(store: store)
                    CommunityShareCard(store: store)
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
        }
        .task(id: store.phase) {
            // Opened to share a shot: bring the Reddit card up once the cards are there.
            guard redditShotID != nil, store.phase == .loaded else { return }
            try? await Task.sleep(for: .milliseconds(300))
            withAnimation { reader.scrollTo("communityRedditCard", anchor: .top) }
        }
        }
        .background(palette.bg)
        .navigationTitle("Community")
        .refreshable { await store.load() }
        .task { await store.load() }
        .task(id: store.notice?.id) {
            guard store.notice != nil else { return }
            try? await Task.sleep(for: .seconds(5))
            if !Task.isCancelled { store.notice = nil }
        }
        .sheet(isPresented: $showSuggest) { CommunitySuggestView(store: store) }
        .sheet(item: $recipeDraft) { draft in
            DiscoverRecipeSheet(draft: draft) { checked in await store.makeRecipe(checked) }
        }
        .accessibilityIdentifier("communityScreen")
    }
}

/// A titled card.
struct CommunityCard<Content: View>: View {
    let title: String
    var subtitle = ""
    let identifier: String
    @ViewBuilder let content: () -> Content

    var body: some View {
        let palette = BrewTheme.shared.palette
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text(title).font(.headline).foregroundStyle(palette.text).accessibilityAddTraits(.isHeader)
                Spacer(minLength: 0)
                if !subtitle.isEmpty { Text(subtitle).font(.footnote).foregroundStyle(palette.dim) }
            }
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(palette.panel, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(identifier)
    }
}

/// What a list says when there is nothing to show.
struct CommunityEmptyNote: View {
    let text: String
    let identifier: String

    var body: some View {
        Text(text)
            .font(.subheadline)
            .foregroundStyle(BrewTheme.shared.palette.dim)
            .frame(maxWidth: .infinity, alignment: .leading)
            .accessibilityIdentifier(identifier)
    }
}
