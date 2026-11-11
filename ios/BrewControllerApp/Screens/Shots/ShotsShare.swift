// Files from the controller (CSV, JSON) handed to the iOS share sheet, and the screen's look and notices.
import SwiftUI
import UIKit

/// A file on disk that the share sheet offers.
struct ShotsSharedFile: Identifiable {
    let id = UUID()
    let url: URL

    /// Writes a download to the temporary folder under the name the controller gave it.
    static func make(_ download: BrewDownload) -> ShotsSharedFile? {
        let safeName = download.filename.replacingOccurrences(of: "/", with: "-")
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            let url = folder.appendingPathComponent(safeName.isEmpty ? "shots.csv" : safeName)
            try download.data.write(to: url, options: .atomic)
            return ShotsSharedFile(url: url)
        } catch {
            return nil
        }
    }
}

/// UIActivityViewController as a sheet.
struct ShotsActivityView: UIViewControllerRepresentable {
    let url: URL

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }

    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

extension View {
    /// The board's colours behind a list.
    @MainActor
    func shotsThemed() -> some View {
        let palette = BrewTheme.shared.palette
        return self
            .scrollContentBackground(.hidden)
            .background(palette.bg.ignoresSafeArea())
            .tint(palette.accent)
    }

    /// The store's error as an alert, its notice as a short banner, and the share sheet.
    @MainActor
    func shotsNotices(_ store: ShotsStore) -> some View {
        modifier(ShotsNoticeModifier(store: store))
    }

    /// Presents the share sheet for a file.
    func shotsShareSheet(_ file: Binding<ShotsSharedFile?>) -> some View {
        sheet(item: file) { item in
            ShotsActivityView(url: item.url).ignoresSafeArea()
        }
    }
}

private struct ShotsNoticeModifier: ViewModifier {
    let store: ShotsStore

    func body(content: Content) -> some View {
        content
            .overlay(alignment: .bottom) {
                if let notice = store.notice {
                    Text(notice)
                        .font(.subheadline)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 10)
                        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 16))
                        .padding(.horizontal, 16)
                        .padding(.bottom, 12)
                        .transition(.opacity)
                        .accessibilityIdentifier("shotsNotice")
                }
            }
            .animation(.default, value: store.notice)
            .alert("Brew Controller", isPresented: Binding(
                get: { store.errorText != nil },
                set: { if !$0 { store.errorText = nil } }
            )) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(store.errorText ?? "")
            }
    }
}

/// One tile: a title, a big value and a line under it (the web page's `.tile`).
struct ShotsTile: View {
    let title: String
    let value: String
    let detail: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title3.weight(.semibold)).minimumScaleFactor(0.6).lineLimit(1)
            Text(detail).font(.caption2).foregroundStyle(.secondary).lineLimit(2)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(BrewTheme.shared.palette.panel, in: RoundedRectangle(cornerRadius: 10))
        .accessibilityElement(children: .combine)
    }
}
