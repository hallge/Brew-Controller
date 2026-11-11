// The board's HOME panel and the web page's Home: status line with the scale and bot dots, the recipe
// card (tap for the recipe list), the hint or Start, Tare, Next recipe, Edit recipe and Coffee keys,
// the alerts, and (HomeExtras.swift) machine power, grinder card, most recent shot, For you and
// Community. All words come from /api/screen.
import SwiftUI

struct HomePanel: View {
    var screen: ScreenSnapshot
    var onRecipes: () -> Void
    var onEdit: () -> Void
    var onCoffee: () -> Void

    private var live: BrewLive { LiveModel.shared.live }
    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        let home = screen.home
        ScrollView {
            VStack(spacing: 14) {
                statusLine(home)
                card(home.card)
                startArea(home)
                keys
                alerts
                HomeExtras()
            }
            .padding(16)
        }
        .accessibilityIdentifier("homePanel")
    }

    // MARK: Status line

    private func statusLine(_ home: ScreenHome) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(home.status)
                .font(.title3.weight(.semibold))
                .foregroundStyle(pal.text)
                .accessibilityIdentifier("statusLine")
            HStack(spacing: 18) {
                link("SCALE", dot: home.scaleDot, battery: home.scaleBattery, tone: home.scaleBatteryTone)
                    .accessibilityIdentifier("scaleLink")
                link("BOT", dot: home.botDot, battery: home.botBattery, tone: home.botBatteryTone)
                    .accessibilityIdentifier("botLink")
                Spacer(minLength: 0)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func link(_ name: String, dot: String, battery: Int?, tone: String?) -> some View {
        HStack(spacing: 6) {
            Text(name).font(.subheadline).foregroundStyle(pal.dim)
            BrewDot(tone: dot)
            if let battery {
                Text("\(battery)%").font(.subheadline).foregroundStyle(pal.color(forTone: tone))
            }
        }
        .accessibilityElement(children: .combine)
    }

    // MARK: Recipe card

    private func card(_ card: ScreenCard) -> some View {
        Button(action: onRecipes) {
            BrewCard {
                VStack(alignment: .leading, spacing: 8) {
                    HStack(alignment: .firstTextBaseline) {
                        Text(card.name).font(.title2.weight(.semibold)).foregroundStyle(pal.accent)
                            .accessibilityIdentifier("cardName")
                        Spacer(minLength: 8)
                        Image(systemName: "chevron.down").foregroundStyle(pal.dim)
                    }
                    Text(card.useRatio ? card.yieldRatio : card.yieldLine)
                        .font(.title3).foregroundStyle(pal.text)
                        .accessibilityIdentifier("cardYield")
                    HStack(alignment: .top, spacing: 16) {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(card.grind).foregroundStyle(pal.text)
                            if !card.advice.isEmpty {
                                Text(card.advice).foregroundStyle(pal.color(forTone: card.adviceTone))
                            }
                        }
                        Spacer(minLength: 0)
                        VStack(alignment: .leading, spacing: 4) {
                            Text(card.pre).foregroundStyle(pal.text)
                            Text(card.foot).foregroundStyle(pal.text)
                        }
                    }
                    .font(.body)
                    if !card.roastAge.isEmpty {
                        Text(card.roastAge).font(.subheadline).foregroundStyle(pal.dim)
                    }
                }
            }
        }
        .buttonStyle(.plain)
        .accessibilityIdentifier("homeCard")
    }

    // MARK: Start

    private func startArea(_ home: ScreenHome) -> some View {
        VStack(spacing: 8) {
            if !home.hint.isEmpty {
                Text(home.hint)
                    .font(.headline)
                    .foregroundStyle(pal.color(forTone: home.hintTone))
                    .multilineTextAlignment(.center)
                    .accessibilityIdentifier("hintLabel")
            }
            Button("START") { LiveModel.shared.send(.start) }
                .buttonStyle(BrewKeyStyle(kind: .ok))
                .disabled(!home.canStart)
                .accessibilityIdentifier("startButton")
        }
    }

    // MARK: Keys

    private var keys: some View {
        VStack(spacing: 10) {
            HStack(spacing: 10) {
                Button("TARE") { LiveModel.shared.send(.tare) }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .disabled(!live.scaleConnected)
                    .accessibilityIdentifier("tareButton")
                Button("NEXT RECIPE") { Task { await model.nextRecipe() } }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .accessibilityIdentifier("nextRecipeButton")
            }
            HStack(spacing: 10) {
                Button(action: onEdit) { Text("EDIT\nRECIPE") }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .disabled(model.activePreset == nil)
                    .accessibilityIdentifier("editRecipeButton")
                Button("COFFEE", action: onCoffee)
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .accessibilityIdentifier("coffeeButton")
            }
        }
    }

    // MARK: Alerts

    /// The web page's two alerts on Home: the stop that has to be done by hand, and a SwitchBot left in Switch mode.
    @ViewBuilder private var alerts: some View {
        if live.manualStopNeeded {
            alert("STOP THE MACHINE BY HAND. The stop press was not delivered or confirmed.", id: "manualStopAlert")
        } else if model.botSwitchMode {
            alert("The SwitchBot is in Switch mode. Set it to Press mode in the SwitchBot app.", id: "switchModeAlert")
        }
    }

    private func alert(_ text: String, id: String) -> some View {
        Text(text)
            .font(.subheadline.weight(.semibold))
            .foregroundStyle(pal.bg)
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(pal.bad, in: RoundedRectangle(cornerRadius: 12))
            .accessibilityIdentifier(id)
    }
}
