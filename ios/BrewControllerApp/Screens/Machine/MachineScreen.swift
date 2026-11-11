// The Machine screen (web: #machine): power, schedule, options, upkeep, make and model, power events.
// Pushed by More inside its NavigationStack, so no NavigationStack of its own.
import SwiftUI

struct MachineScreen: View {
    @State private var store = MachineStore()
    @State private var schedule: [PowerScheduleEntry] = []
    @State private var visibleRows = 1
    @State private var scheduleLoaded = false

    var body: some View {
        List {
            MachineMessages(error: store.error, notice: store.notice)
            powerSection
            scheduleSection
            optionsSection
            upkeepSection
            Section {
                NavigationLink {
                    MachineMakeModelView(store: store)
                } label: {
                    VStack(alignment: .leading) {
                        Text("Make and model")
                        Text("\(store.machine.current.maker) \(store.machine.current.model)")
                            .font(.footnote).foregroundStyle(.secondary)
                            .accessibilityIdentifier("machineCurrentName")
                    }
                }
                .accessibilityIdentifier("machineMakeModelLink")
            }
            eventsSection
        }
        .navigationTitle("Espresso machine")
        .accessibilityIdentifier("machineScreen")
        .task {
            await store.refresh()
            if !scheduleLoaded {
                schedule = store.power.schedule
                let lastUsed = schedule.enumerated().reduce(0) { $1.element.enabled ? $1.offset + 1 : $0 }
                visibleRows = max(1, lastUsed)
                scheduleLoaded = true
            }
        }
        .refreshable { await store.refresh() }
    }

    // MARK: Power

    private var beliefText: String {
        switch store.power.belief {
        case "on": return "On"
        case "off": return "Off"
        default: return "Not known"
        }
    }

    private var powerLine: String {
        let p = store.power
        var bits: [String] = []
        if !p.paired { bits.append("No power bot paired. Pick one marked \"Power\" in Setup › Devices.") }
        if p.belief == "unknown" { bits.append("Say whether it is on or off below.") }
        if p.working { bits.append("Pressing the power button…") }
        if !p.last.isEmpty { bits.append("Last: \(p.last)" + (p.lastBySchedule ? " (schedule)" : "")) }
        if !p.clockSet { bits.append("The clock is not set: the schedule waits for it.") }
        return bits.joined(separator: " · ")
    }

    private var powerSection: some View {
        Section("Machine power") {
            HStack {
                Text("State")
                Spacer()
                Text(beliefText)
                    .fontWeight(.semibold)
                    .foregroundStyle(store.power.belief == "on" ? Color.green : store.power.belief == "off" ? Color.secondary : Color.orange)
                    .accessibilityIdentifier("machinePowerState")
            }
            if !powerLine.isEmpty {
                Text(powerLine).font(.footnote).foregroundStyle(.secondary).accessibilityIdentifier("machinePowerLine")
            }
            HStack {
                Button("Switch on") { Task { await store.powerAction("on") } }
                    .buttonStyle(.borderedProminent)
                    .accessibilityIdentifier("machineSwitchOn")
                Button("Switch off") { Task { await store.powerAction("off") } }
                    .buttonStyle(.bordered)
                    .accessibilityIdentifier("machineSwitchOff")
            }
            .disabled(!store.power.paired)
            HStack {
                Text("Not right? It is really").font(.footnote).foregroundStyle(.secondary)
                Button("on") { Task { await store.powerAction("believe_on") } }
                    .accessibilityIdentifier("machineBelieveOn")
                Button("off") { Task { await store.powerAction("believe_off") } }
                    .accessibilityIdentifier("machineBelieveOff")
            }
            .buttonStyle(.bordered)
            Button("Press the button anyway") { Task { await store.powerAction("press") } }
                .disabled(!store.power.paired)
                .accessibilityIdentifier("machinePressAnyway")
            Text("Switched by hand, or it switched itself off? Say how it is: nothing is pressed. The power button is a toggle and the machine does not say whether it is on, so the controller keeps count. \"Press the button anyway\" presses it whatever the controller believes.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    // MARK: Schedule

    private var scheduleSection: some View {
        Section {
            ForEach(0..<min(visibleRows, schedule.count), id: \.self) { index in
                MachineScheduleRow(entry: $schedule[index], number: index + 1)
            }
            if visibleRows < schedule.count {
                Button("Add a time") { visibleRows += 1 }
                    .accessibilityIdentifier("machineScheduleAdd")
            }
            Button("Save schedule") { Task { await store.saveSchedule(schedule) } }
                .accessibilityIdentifier("machineScheduleSave")
        } header: {
            Text("Schedule")
        } footer: {
            Text("A time is skipped if the machine is already that way, or a shot is running. Times are this phone's local time; save again after a daylight-saving change.")
        }
    }

    // MARK: Options

    private var optionsSection: some View {
        Section("Options") {
            Toggle(isOn: Binding(get: { store.settings.keepPowerBotConnected },
                                 set: { value in
                                     store.settings.keepPowerBotConnected = value
                                     Task { await store.saveOption("keepPowerBotConnected", .bool(value)) }
                                 })) {
                Text("Keep the power bot connected while the scale is off (presses at once; uses more battery)")
            }
            .accessibilityIdentifier("machineKeepPowerBot")
            Toggle(isOn: Binding(get: { store.settings.powerOffWithScale },
                                 set: { value in
                                     store.settings.powerOffWithScale = value
                                     Task { await store.saveOption("powerOffWithScale", .bool(value)) }
                                 })) {
                Text("Switch the machine off when the scale switches off (only when it is known to be on)")
            }
            .disabled(!store.power.paired)
            .accessibilityIdentifier("machineOffWithScale")
            Stepper(value: Binding(get: { store.settings.scaleRestMin },
                                   set: { value in
                                       store.settings.scaleRestMin = value
                                       Task { await store.saveOption("scaleRestMin", .int(value)) }
                                   }), in: 0...60) {
                Text(store.settings.scaleRestMin == 0 ? "Let the scale sleep after: never (keep it connected)"
                     : "Let the scale sleep after \(store.settings.scaleRestMin) minutes idle")
            }
            .accessibilityIdentifier("machineScaleRest")
            Text("An Acaia scale only counts down to sleep while nothing is connected to it, so the controller lets go of it. The brew bot is let go with it; the power bot connects once the scale is off.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    // MARK: Upkeep

    private var upkeepSection: some View {
        Section("Upkeep") {
            Text("\(store.maintenance.shotsTotal) shots counted").font(.footnote).foregroundStyle(.secondary)
            MachineUpkeepRow(job: store.job("backflush"), title: "Backflush", idPrefix: "machineBackflush",
                             onDone: { await store.upkeepDone("backflush") },
                             onEvery: { await store.upkeepEvery("backflush", $0) })
            MachineUpkeepRow(job: store.job("descale"), title: "Descale", idPrefix: "machineDescale",
                             onDone: { await store.upkeepDone("descale") },
                             onEvery: { await store.upkeepEvery("descale", $0) })
        }
    }

    // MARK: Events

    private var eventsSection: some View {
        Section("Power events") {
            if store.events.isEmpty {
                Text("No power button presses in the last 30 days.").font(.footnote).foregroundStyle(.secondary)
                    .accessibilityIdentifier("machineEventsEmpty")
            }
            ForEach(store.events.prefix(20)) { event in
                HStack {
                    VStack(alignment: .leading) {
                        Text(event.on ? "Switched on" : "Switched off")
                        Text(Self.eventLine(event)).font(.footnote).foregroundStyle(.secondary)
                    }
                    Spacer()
                    if !event.ok { Image(systemName: "exclamationmark.triangle").foregroundStyle(.orange) }
                }
            }
        }
    }

    static func eventLine(_ event: MachinePowerEvent) -> String {
        let when = Date(timeIntervalSince1970: TimeInterval(event.t)).formatted(date: .abbreviated, time: .shortened)
        let who = event.by == "schedule" ? "schedule" : event.by == "scale" ? "with the scale" : "by you"
        return "\(when) · \(who)" + (event.outcome.isEmpty ? "" : " · \(event.outcome)") + (event.sure ? "" : " · not sure")
    }
}

/// One row of the schedule: in use, on or off, the time and the seven weekdays.
struct MachineScheduleRow: View {
    @Binding var entry: PowerScheduleEntry
    var number: Int

    private static let days = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"]

    private var time: Binding<Date> {
        Binding(
            get: {
                var parts = DateComponents()
                parts.hour = entry.minute / 60
                parts.minute = entry.minute % 60
                return Calendar.current.date(from: parts) ?? Date()
            },
            set: { value in
                let parts = Calendar.current.dateComponents([.hour, .minute], from: value)
                entry.minute = (parts.hour ?? 0) * 60 + (parts.minute ?? 0)
            })
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Toggle("In use", isOn: $entry.enabled)
                    .accessibilityIdentifier("machineScheduleUse\(number)")
                Picker("Switch", selection: $entry.on) {
                    Text("On").tag(true)
                    Text("Off").tag(false)
                }
                .pickerStyle(.segmented)
                .frame(width: 110)
                .accessibilityIdentifier("machineScheduleOn\(number)")
            }
            DatePicker("At", selection: time, displayedComponents: .hourAndMinute)
                .accessibilityIdentifier("machineScheduleTime\(number)")
            HStack(spacing: 4) {
                ForEach(0..<7, id: \.self) { day in
                    let on = (entry.days >> day) & 1 == 1
                    Button(Self.days[day]) { entry.days ^= (1 << day) }
                        .buttonStyle(.bordered)
                        .tint(on ? Color.accentColor : Color.gray)
                        .font(.caption)
                        .accessibilityIdentifier("machineScheduleDay\(number)_\(day)")
                }
            }
        }
        .padding(.vertical, 4)
    }
}

/// An error in red and a notice in green, at the top of a list.
struct MachineMessages: View {
    var error: String?
    var notice: String?

    var body: some View {
        if let error {
            Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("machineError") }
        } else if let notice {
            Section { Text(notice).foregroundStyle(.green).accessibilityIdentifier("machineNotice") }
        }
    }
}
