// "Share a shot to Reddit": a 1200 x 1200 picture of the shot (its curve, the recipe and the coffee, and
// your photo if you add one) and the post's text, handed to the share sheet. Nothing is sent from the
// controller. The picture is drawn by SwiftUI and rendered with ImageRenderer.
import PhotosUI
import SwiftUI
import UIKit

struct CommunityRedditCard: View {
    let store: CommunityStore

    /// `initialShotID`: the shot to preselect (the Brew tab and the shot page push the Community screen with one).
    init(store: CommunityStore, initialShotID: Int? = nil) {
        self.store = store
        _shotId = State(initialValue: initialShotID ?? 0)
    }

    @Environment(\.openURL) private var openURL
    @State private var shotId = 0
    @State private var photoItem: PhotosPickerItem?
    @State private var photo: UIImage?
    @State private var subreddit = "espresso"
    @State private var title = ""
    @State private var lastAutoTitle = ""
    @State private var titleShot = 0
    @State private var text = ""
    @State private var curve: ShotDataModel?
    @State private var picture: UIImage?
    @State private var copied = false

    private var selected: ShotModel? { store.shots.first { $0.id == shotId } ?? store.shots.first }

    private var item: CommunityText.RedditShot? {
        guard let shot = selected else { return nil }
        return CommunityText.RedditShot(shot: shot, coffee: store.library.coffee(id: shot.coffeeId),
                                        grinder: store.library.grinder)
    }

    var body: some View {
        let palette = BrewTheme.shared.palette
        CommunityCard(title: "Share a shot to Reddit", subtitle: "Nothing is sent from the controller",
                      identifier: "communityReddit") {
            Text("A picture of the shot (its curve, the recipe and the coffee, and your photo if you add one) and the post's text, ready for r/espresso.")
                .font(.footnote).foregroundStyle(palette.dim)
            if store.shots.isEmpty {
                CommunityEmptyNote(text: "Brew a shot first: it can then be shared from here.", identifier: "communityRedditEmpty")
            } else {
                Picker("Shot", selection: Binding(get: { selected?.id ?? 0 }, set: { shotId = $0 })) {
                    ForEach(store.shots) { shot in
                        Text(shotLabel(shot)).tag(shot.id)
                    }
                }
                .pickerStyle(.menu)
                .accessibilityIdentifier("communityRedditShot")
                HStack {
                    PhotosPicker(selection: $photoItem, matching: .images) {
                        Label(photo == nil ? "Your photo (optional)" : "Change photo", systemImage: "photo")
                    }
                    .accessibilityIdentifier("communityRedditPhoto")
                    if photo != nil {
                        Button("Remove", role: .destructive) { photo = nil; photoItem = nil }
                    }
                }
                .font(.subheadline)
                Picker("Community", selection: $subreddit) {
                    ForEach(CommunityText.subreddits, id: \.key) { sub in Text(sub.label).tag(sub.key) }
                }
                .pickerStyle(.segmented)
                .accessibilityIdentifier("communityRedditSub")
                TextField("Title", text: $title, axis: .vertical)
                    .lineLimit(1...3)
                    .textFieldStyle(.roundedBorder)
                    .onChange(of: title) { _, value in if value.count > 300 { title = String(value.prefix(300)) } }
                    .accessibilityIdentifier("communityRedditTitle")
                if let picture {
                    Image(uiImage: picture)
                        .resizable()
                        .aspectRatio(1, contentMode: .fit)
                        .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                        .overlay(RoundedRectangle(cornerRadius: 10, style: .continuous).stroke(palette.track, lineWidth: 1))
                        .accessibilityLabel("The picture of the shot for Reddit")
                        .accessibilityIdentifier("communityRedditImage")
                }
                TextEditor(text: $text)
                    .frame(minHeight: 160)
                    .scrollContentBackground(.hidden)
                    .padding(6)
                    .background(palette.track.opacity(0.4), in: RoundedRectangle(cornerRadius: 10, style: .continuous))
                    .accessibilityIdentifier("communityRedditText")
                HStack(spacing: 8) {
                    if let picture {
                        ShareLink(item: Image(uiImage: picture),
                                  preview: SharePreview("Espresso shot", image: Image(uiImage: picture))) {
                            Label("Share image", systemImage: "square.and.arrow.up")
                        }
                        .accessibilityIdentifier("communityRedditShareImage")
                    }
                    ShareLink(item: text) { Label("Share text", systemImage: "text.alignleft") }
                        .accessibilityIdentifier("communityRedditShareText")
                }
                .buttonStyle(.borderedProminent)
                .font(.subheadline)
                HStack(spacing: 8) {
                    Button(copied ? "Copied" : "Copy text") {
                        UIPasteboard.general.string = text
                        copied = true
                    }
                    .accessibilityIdentifier("communityRedditCopy")
                    Button("Open Reddit") {
                        let encoded = title.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? ""
                        if let url = URL(string: "https://www.reddit.com/r/\(subreddit)/submit?title=\(encoded)") {
                            openURL(url)
                        }
                    }
                    .accessibilityIdentifier("communityRedditOpen")
                }
                .buttonStyle(.bordered)
                .font(.subheadline)
                Text("Reddit takes no picture through a link: share the image, then on Reddit attach it to the post and paste the text.")
                    .font(.footnote).foregroundStyle(palette.dim)
            }
        }
        .task(id: selected?.id) { await shotChanged() }
        .onChange(of: photoItem) { _, newItem in
            guard let newItem else { return }
            Task {
                if let data = try? await newItem.loadTransferable(type: Data.self), let image = UIImage(data: data) {
                    photo = image
                }
            }
        }
        .task(id: renderKey) { renderPicture() }
    }

    private func shotLabel(_ shot: ShotModel) -> String {
        let when = shot.date?.formatted(.dateTime.month(.abbreviated).day().hour().minute()) ?? "shot \(shot.id)"
        let name = shot.coffee.isEmpty ? shot.recipe : shot.coffee
        return "\(when) · \(name) · \(BrewFormat.grams(shot.finalWeightG)) g in \(BrewFormat.seconds(shot.shotTimeS)) s"
    }

    /// A new shot: its curve, the title and the text.
    private func shotChanged() async {
        guard let shot = selected else { return }
        curve = await store.shotData(shot)
        if let item {
            // A new shot always gets a new title; the same shot keeps what the owner typed.
            let automatic = CommunityText.redditTitle(item)
            if title.isEmpty || title == lastAutoTitle || titleShot != shot.id { title = automatic }
            lastAutoTitle = automatic
            titleShot = shot.id
            text = CommunityText.redditText(item)
        }
    }

    /// What the picture depends on.
    private var renderKey: String {
        "\(selected?.id ?? 0)-\(curve?.points.count ?? 0)-\(photo.map { "\($0.size.width)x\($0.size.height)" } ?? "none")-\(BrewTheme.shared.current.rawValue)"
    }

    private func renderPicture() {
        guard let item else {
            picture = nil
            return
        }
        let palette = BrewTheme.shared.palette
        let view = CommunityRedditImage(item: item, points: curve?.points ?? [], photo: photo,
                                        background: palette.bg, panel: palette.panel, text: palette.text,
                                        dim: palette.dim, accent: palette.accent, flow: palette.ok)
            .frame(width: 1200, height: 1200)
        let renderer = ImageRenderer(content: view)
        renderer.scale = 1
        picture = renderer.uiImage
    }
}

/// The 1200 x 1200 picture: coffee, curve, the numbers and (optionally) the owner's photo.
struct CommunityRedditImage: View {
    let item: CommunityText.RedditShot
    let points: [ShotPoint]
    let photo: UIImage?
    let background: Color
    let panel: Color
    let text: Color
    let dim: Color
    let accent: Color
    let flow: Color

    var body: some View {
        let rows = CommunityText.redditRows(item)
        let detail = [item.coffee?.roaster, item.coffee?.origin, item.coffee?.process]
            .compactMap { $0 }.filter { !$0.isEmpty }.joined(separator: " · ")
        VStack(alignment: .leading, spacing: 0) {
            Text(String(item.name.prefix(34))).font(.system(size: 52, weight: .semibold)).foregroundStyle(text)
            Text(String(detail.prefix(70))).font(.system(size: 28)).foregroundStyle(dim).padding(.top, 8)
            ZStack {
                panel
                if points.count > 1 {
                    CommunityCurve(points: points, weightColor: accent, flowColor: flow)
                        .padding(30)
                } else {
                    Text("No curve stored for this shot").font(.system(size: 28)).foregroundStyle(dim)
                }
            }
            .frame(height: 500)
            .padding(.top, 24)
            HStack(alignment: .top, spacing: 24) {
                VStack(alignment: .leading, spacing: 12) {
                    ForEach(rows.indices, id: \.self) { index in
                        HStack(alignment: .top, spacing: 20) {
                            Text(rows[index].label).font(.system(size: 30)).foregroundStyle(dim).frame(width: 150, alignment: .leading)
                            Text(rows[index].value).font(.system(size: 30)).foregroundStyle(text)
                                .lineLimit(2).minimumScaleFactor(0.6)
                        }
                    }
                }
                .padding(30)
                .frame(maxWidth: .infinity, alignment: .topLeading)
                .frame(height: 440, alignment: .topLeading)
                .background(panel)
                if let photo {
                    Image(uiImage: photo)
                        .resizable()
                        .scaledToFill()
                        .frame(width: 460, height: 440)
                        .clipped()
                }
            }
            .padding(.top, 24)
            Spacer(minLength: 0)
            Text("Brew by Weight controller · " + CommunityText.footerLink)
                .font(.system(size: 22)).foregroundStyle(dim)
        }
        .padding(48)
        .frame(width: 1200, height: 1200, alignment: .topLeading)
        .background(background)
    }
}

/// The weight and flow curves of a shot, scaled to the box they are drawn in.
struct CommunityCurve: View {
    let points: [ShotPoint]
    let weightColor: Color
    let flowColor: Color

    var body: some View {
        GeometryReader { geometry in
            let width = geometry.size.width
            let height = geometry.size.height
            let maxTime = max(1, points.map { $0.time }.max() ?? 1)
            let maxWeight = max(1, points.map { $0.weight }.max() ?? 1)
            let maxFlow = max(0.1, points.map { $0.flow }.max() ?? 0.1)
            ZStack {
                Path { path in
                    for (index, point) in points.enumerated() {
                        let p = CGPoint(x: width * point.time / maxTime, y: height * (1 - point.weight / maxWeight))
                        if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
                    }
                }
                .stroke(weightColor, style: StrokeStyle(lineWidth: 6, lineCap: .round, lineJoin: .round))
                Path { path in
                    for (index, point) in points.enumerated() {
                        let p = CGPoint(x: width * point.time / maxTime, y: height * (1 - point.flow / maxFlow))
                        if index == 0 { path.move(to: p) } else { path.addLine(to: p) }
                    }
                }
                .stroke(flowColor, style: StrokeStyle(lineWidth: 4, lineCap: .round, lineJoin: .round))
            }
        }
    }
}
