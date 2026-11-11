// The board's SETTINGS panel (the gear, Ui.cpp buildSettings/refreshSettings), as a sheet from the Brew
// tab: the firmware version, OFFSET LEARNING / FIXED, tare, the bot and the scale with their state and
// battery (the SwitchBot's Switch mode flagged), pair the nearest bot, forget the bot or the scale. The
// board's ROTATE SCREEN and Wi-Fi lines have no use on the phone.
import SwiftUI

struct BrewSettingsView: View {
    @Environment(\.dismiss) private var dismiss
    private var model: BrewModel { BrewModel.shared }
    private var live: BrewLive { LiveModel.shared.live }
    @State private var confirmForget: String?
    @State private var note: String?

    private var devices: DevicesModel? { model.devices }

    // MARK: Words (refreshSettings)

    private var botText: String {
        guard let d = devices else { return "" }
        let battery = d.botBattery.map { "battery \($0)%" } ?? ""
        if d.pairing.bot.isEmpty { return "Bot: not paired\nhold it close, tap PAIR" }
        if d.botConnected && d.botSwitchMode {
            return "Bot: connected" + (d.botBattery.map { ", \($0)%" } ?? "") + "\nSWITCH MODE: use Press"
        }
        if d.botConnected { return "Bot: connected\n" + battery }
        return "Bot: asleep until needed\n" + battery
    }

    private var scaleText: String {
        guard let d = devices else { return "" }
        if d.scaleConnected {
            let battery = (model.status?.scaleBattery).map { $0 >= 0 ? "battery \($0)%" : "" } ?? ""
            return "Scale: \(d.scaleName)\n" + (battery.isEmpty ? d.scaleProtocol : battery)
        }
        return "Scale: " + (d.pairing.scale.isEmpty ? "first scale found" : "searching") + "\n" + d.pairing.scale
    }

    var body: some View {
        NavigationStack {
            ZStack {
                brewPal.bg.ignoresSafeArea()
                ScrollView {
                    VStack(spacing: 14) {
                        if !model.version.isEmpty {
                            Text("Firmware \(model.version)").font(.subheadline).foregroundStyle(brewPal.dim)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .accessibilityIdentifier("settingsVersion")
                        }
                        let learning = model.settings?.learningEnabled ?? true
                        Button(learning ? "OFFSET: LEARNING" : "OFFSET: FIXED") {
                            Task { await model.setLearning(!learning) }
                        }
                        .buttonStyle(BrewKeyStyle(kind: .panel))
                        .accessibilityIdentifier("settingsLearning")
                        Text(learning ? "The stop offset is learned from every shot." : "The stop offset stays as it is.")
                            .font(.footnote).foregroundStyle(brewPal.dim)
                            .frame(maxWidth: .infinity, alignment: .leading)

                        Button("TARE SCALE") { LiveModel.shared.send(.tare) }
                            .buttonStyle(BrewKeyStyle(kind: .panel))
                            .disabled(!live.scaleConnected)
                            .accessibilityIdentifier("settingsTare")

                        deviceBlock(title: botText, id: "settingsBotStatus", warn: devices?.botSwitchMode == true && devices?.botConnected == true) {
                            Button("PAIR NEAREST BOT") {
                                Task {
                                    let found = await model.pairNearestBot()
                                    note = found ? "Pairing the nearest bot" : "No bot found nearby"
                                }
                            }
                            .buttonStyle(BrewKeyStyle(kind: .panel))
                            .accessibilityIdentifier("settingsPairBot")
                            Button("FORGET BOT") { confirmForget = "bot" }
                                .buttonStyle(BrewKeyStyle(kind: .panel))
                                .disabled(devices?.pairing.bot.isEmpty ?? true)
                                .accessibilityIdentifier("settingsForgetBot")
                        }
                        deviceBlock(title: scaleText, id: "settingsScaleStatus", warn: false) {
                            Button("FORGET SCALE") { confirmForget = "scale" }
                                .buttonStyle(BrewKeyStyle(kind: .panel))
                                .disabled(devices?.pairing.scale.isEmpty ?? true)
                                .accessibilityIdentifier("settingsForgetScale")
                        }
                        if let note {
                            Text(note).font(.footnote).foregroundStyle(brewPal.dim)
                                .accessibilityIdentifier("settingsNote")
                        }
                        Text("More about the devices is under More, Devices.")
                            .font(.footnote).foregroundStyle(brewPal.dim)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .padding(16)
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }.accessibilityIdentifier("settingsDone")
                }
            }
        }
        .task {
            // The board keeps its pairing list fresh while this panel is open.
            while !Task.isCancelled {
                await model.refreshDevices()
                try? await Task.sleep(for: .seconds(3))
            }
        }
        .alert(confirmForget == "bot" ? "Forget the bot?" : "Forget the scale?",
               isPresented: Binding(get: { confirmForget != nil }, set: { if !$0 { confirmForget = nil } })) {
            Button("Forget", role: .destructive) {
                if let kind = confirmForget { Task { await model.forget(kind) } }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("It can be paired again later.")
        }
        .accessibilityIdentifier("settingsSheet")
    }

    private func deviceBlock<Keys: View>(title: String, id: String, warn: Bool, @ViewBuilder keys: () -> Keys) -> some View {
        BrewCard {
            VStack(alignment: .leading, spacing: 10) {
                Text(title)
                    .foregroundStyle(warn ? brewPal.bad : brewPal.text)
                    .accessibilityIdentifier(id)
                keys()
            }
        }
    }
}
