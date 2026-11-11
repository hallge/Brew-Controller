// The Devices screen (web: Setup › Devices; board: Settings): what is paired, the devices in range with
// their roles, pairing, forgetting, swapping, test presses and the pusher keys.
// Pushed by More inside its NavigationStack, so no NavigationStack of its own.
import SwiftUI

struct DevicesScreen: View {
    @State private var store = DevicesStore()
    @State private var confirm: DevicesConfirm?

    var body: some View {
        List {
            if let error = store.error {
                Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("devicesError") }
            } else if let notice = store.notice {
                Section { Text(notice).foregroundStyle(.green).accessibilityIdentifier("devicesNotice") }
            }
            summarySection
            scaleLinkSection
            seenSection
            actionsSection
            Section {
                NavigationLink {
                    DevicesPusherView(store: store)
                } label: {
                    Label("Pusher keys", systemImage: "key")
                }
                .accessibilityIdentifier("devicesPusherLink")
            }
        }
        .navigationTitle("Devices")
        .accessibilityIdentifier("devicesScreen")
        .task { await store.refresh() }
        .refreshable { await store.refresh() }
        .confirmationDialog(confirm?.title ?? "", isPresented: Binding(get: { confirm != nil }, set: { if !$0 { confirm = nil } }),
                            titleVisibility: .visible, presenting: confirm) { item in
            Button(item.button, role: item.destructive ? .destructive : nil) { Task { await run(item) } }
            Button("Cancel", role: .cancel) {}
        } message: { item in
            Text(item.message)
        }
    }

    // MARK: Summary

    private func signalText(_ address: String) -> String {
        store.signal(for: address).map { ", signal \($0) dBm" } ?? ""
    }

    private var summarySection: some View {
        let d = store.devices
        let x = store.detail
        return Section("Paired") {
            DevicesSummaryRow(title: "Scale", id: "devicesScale",
                              state: d.scaleConnected ? "\(d.scaleName) (\(d.scaleProtocol)), connected" : "not connected",
                              address: d.pairing.scale.isEmpty ? "adopts the first supported scale found" : d.pairing.scale,
                              extra: signalText(d.pairing.scale) + (d.scaleData.battery.map { ", battery \($0)%" } ?? ""),
                              problem: "")
            DevicesSummaryRow(title: "Brew bot" + (d.botLabel.isEmpty ? "" : " (\(d.botLabel))"), id: "devicesBot",
                              state: d.botConnected ? "connected" : (d.pairing.bot.isEmpty ? "not paired" : "asleep until Start"),
                              address: d.pairing.bot.isEmpty ? "pick one below" : d.pairing.bot,
                              extra: botExtraText,
                              problem: x.botProblem)
            if !d.botCaps.isEmpty && !d.botCaps.contains("down_up") {
                Text("This pusher cannot hold the button: held pre-infusion and bloom restarts become plain presses.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            DevicesSummaryRow(title: "Power bot" + (d.powerLabel.isEmpty ? "" : " (\(d.powerLabel))"), id: "devicesPower",
                              state: d.powerConnected ? "connected" : (d.pairing.power.isEmpty ? "not paired" : "asleep until needed"),
                              address: d.pairing.power.isEmpty ? "optional: pick one below" : d.pairing.power,
                              extra: signalText(d.pairing.power) + (x.powerBattery.map { ", battery \($0)%" } ?? ""),
                              problem: x.powerProblem)
            if !d.pairing.stop.isEmpty || d.stopConnected {
                DevicesSummaryRow(title: "Stop bot" + (d.stopLabel.isEmpty ? "" : " (\(d.stopLabel))"), id: "devicesStop",
                                  state: d.stopConnected ? "connected" : "asleep until Start",
                                  address: d.pairing.stop,
                                  extra: signalText(d.pairing.stop) + (x.stopBattery.map { ", battery \($0)%" } ?? ""),
                                  problem: x.stopProblem)
            }
        }
    }

    /// Built step by step: as one `+` chain the compiler gives up type-checking it.
    private var scaleLinkText: String {
        let d = store.devices
        let q = d.scaleData
        var text: String = "\(q.weightFrames) readings since it connected"
        if d.scaleCaps.contains("timer") {
            text += ", \(q.timerFrames) with the scale's timer"
        }
        text += ", \(q.splitFrames) frames arrived in two parts, \(q.badFrames) failed their checksum"
        if let battery = q.battery {
            text += ", scale battery \(battery)%"
        }
        return text + "."
    }

    private var botExtraText: String {
        let d = store.devices
        var text: String = signalText(d.pairing.bot)
        if !d.botFirmware.isEmpty && d.botFirmware != "0.0" {
            text += ", firmware \(d.botFirmware)"
        }
        if let battery = d.botBattery {
            text += ", battery \(battery)%"
        }
        return text
    }

    private var scaleLinkSection: some View {
        let d = store.devices
        let q = d.scaleData
        let missing = ["timer_control": "timer control", "timer": "timer readings", "battery": "battery level", "buttons": "button presses"]
            .filter { !d.scaleCaps.contains($0.key) }.map { $0.value }.sorted()
        return Group {
            if d.scaleConnected {
                Section("Scale link") {
                    Text(scaleLinkText)
                        .font(.footnote)
                        .accessibilityIdentifier("devicesScaleLink")
                    if q.zeroesItself {
                        Text("The scale tares itself (an auto-tare mode). A cup taken off can then go unseen, so a shot could start with no cup: set the scale to a mode without auto-tare.")
                            .font(.footnote).foregroundStyle(.orange)
                    }
                    if !missing.isEmpty {
                        Text("This scale does not offer: \(missing.joined(separator: ", ")).").font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
        }
    }

    // MARK: Seen

    private var seenSection: some View {
        Section {
            if store.devices.seen.isEmpty {
                Text("Nothing in range yet.").font(.footnote).foregroundStyle(.secondary)
            }
            ForEach(store.devices.seen) { device in
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        VStack(alignment: .leading) {
                            Text(device.name.isEmpty ? device.address : device.name)
                            Text("\(device.kind) · \(device.address) · \(device.rssi) dBm")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                    }
                    HStack {
                        if device.kind == "bot" {
                            roleButton(device, role: "bot", label: "Brew")
                            roleButton(device, role: "power", label: "Power")
                            roleButton(device, role: "stop", label: "Stop")
                        } else {
                            roleButton(device, role: "scale", label: "Use")
                        }
                    }
                }
                .padding(.vertical, 2)
                .accessibilityElement(children: .contain)
                .accessibilityIdentifier("devicesSeen_\(device.address)")
            }
            Button {
                Task { await store.refresh() }
            } label: {
                Label("Refresh", systemImage: "arrow.clockwise")
            }
            .buttonStyle(.bordered)
            .accessibilityIdentifier("devicesRefresh")
        } header: {
            Text("Seen")
        } footer: {
            Text("Use puts a scale to work; Brew, Power and Stop assign a bot to that job.")
        }
    }

    private func roleButton(_ device: SeenDevice, role: String, label: String) -> some View {
        let on = store.holds(role, device)
        return Button((on ? "✓ " : "") + label) {
            Task { await store.pair(role, address: device.address, type: device.type) }
        }
        .buttonStyle(.bordered)
        .tint(on ? Color.accentColor : Color.gray)
        .accessibilityIdentifier("devicesRole_\(device.address)_\(role)")
    }

    // MARK: Actions

    private var actionsSection: some View {
        Section("Pair, test and forget") {
            Button("Pair the nearest bot") { Task { await store.pairNearestBot() } }
                .accessibilityIdentifier("devicesPairNearest")
            Button("Test press: brew bot") { confirm = .testBrew }
                .accessibilityIdentifier("devicesTestBrew")
            Button("Test press: power bot") { confirm = .testPower }
                .accessibilityIdentifier("devicesTestPower")
            Button("Swap brew and power") { confirm = .swap }
                .accessibilityIdentifier("devicesSwap")
            Button("Forget the scale", role: .destructive) { confirm = .forget("scale", "scale") }
                .accessibilityIdentifier("devicesForgetScale")
            Button("Forget the brew bot", role: .destructive) { confirm = .forget("bot", "brew bot") }
                .accessibilityIdentifier("devicesForgetBot")
            Button("Forget the power bot", role: .destructive) { confirm = .forget("power", "power bot") }
                .accessibilityIdentifier("devicesForgetPower")
            Button("Forget the stop bot", role: .destructive) { confirm = .forget("stop", "stop bot") }
                .accessibilityIdentifier("devicesForgetStop")
        }
    }

    private func run(_ item: DevicesConfirm) async {
        switch item {
        case .testBrew: await store.testPress(role: "brew")
        case .testPower: await store.testPress(role: "power")
        case .swap: await store.swapBots()
        case .forget(let kind, let label): await store.forget(kind, label: label)
        }
    }
}

/// The questions the screen asks before it acts.
enum DevicesConfirm: Identifiable {
    case testBrew, testPower, swap
    case forget(String, String)

    var id: String {
        switch self {
        case .testBrew: return "testBrew"
        case .testPower: return "testPower"
        case .swap: return "swap"
        case .forget(let kind, _): return "forget-\(kind)"
        }
    }

    var title: String {
        switch self {
        case .testBrew: return "Press the brew bot once?"
        case .testPower: return "Press the power bot once?"
        case .swap: return "Swap them?"
        case .forget(_, let label): return "Forget the \(label)?"
        }
    }

    var message: String {
        switch self {
        case .testBrew: return "If it is on the machine and the machine is on, the pump STARTS. Press again to stop it."
        case .testPower: return "The machine switches on or off."
        case .swap: return "The brew bot becomes the power bot and the power bot becomes the brew bot."
        case .forget: return "It will have to be paired again before it is used."
        }
    }

    var button: String {
        switch self {
        case .testBrew, .testPower: return "Press"
        case .swap: return "Swap"
        case .forget: return "Forget"
        }
    }

    var destructive: Bool {
        if case .forget = self { return true }
        return false
    }
}

/// One line of the paired-devices summary.
struct DevicesSummaryRow: View {
    var title: String
    var id: String
    var state: String
    var address: String
    var extra: String
    var problem: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).fontWeight(.semibold)
                Spacer()
                Text(state).foregroundStyle(.secondary).accessibilityIdentifier("\(id)State")
            }
            Text(address + extra).font(.caption).foregroundStyle(.secondary)
            if !problem.isEmpty { Text(problem).font(.caption).foregroundStyle(.orange) }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier(id)
    }
}
