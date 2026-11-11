// One upkeep job (backflush, descale, clean the grinder): "n shots since, last done, next in", the
// "Remind after" field (shots, 0 = never) and "Done today". Used by the Machine and Grinder screens.
import SwiftUI

struct MachineUpkeepRow: View {
    var job: MaintenanceJob?
    var title: String
    var idPrefix: String
    var onDone: () async -> Void
    var onEvery: (Int) async -> Void

    @State private var everyText = ""
    @State private var synced = false
    @FocusState private var focused: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).fontWeight(.semibold)
            if let job {
                Text(status(job)).font(.footnote).foregroundStyle(job.due ? Color.red : Color.secondary)
                    .accessibilityIdentifier("\(idPrefix)Status")
                HStack {
                    Text("Remind after")
                    TextField("never", text: $everyText)
                        .keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing)
                        .frame(width: 70)
                        .accessibilityIdentifier("\(idPrefix)Every")
                        .focused($focused)
                        .onChange(of: focused) { _, now in
                            if !now, changed { commit() }
                        }
                        .toolbar {
                            if focused {
                                ToolbarItemGroup(placement: .keyboard) {
                                    Spacer()
                                    Button("Set") { commit(); focused = false }
                                }
                            }
                        }
                    Text("shots").foregroundStyle(.secondary)
                    if changed {
                        Button("Set") { commit(); focused = false }
                            .buttonStyle(.bordered)
                            .accessibilityIdentifier("\(idPrefix)EverySet")
                    }
                    Spacer()
                    Button("Done today") { Task { await onDone() } }
                        .buttonStyle(.bordered)
                        .accessibilityIdentifier("\(idPrefix)Done")
                }
            } else {
                Text("Not available.").font(.footnote).foregroundStyle(.secondary)
            }
        }
        .onChange(of: job?.everyShots) { _, value in everyText = (value ?? 0) > 0 ? String(value ?? 0) : "" }
        .onAppear {
            if !synced, let job {
                everyText = job.everyShots > 0 ? String(job.everyShots) : ""
                synced = true
            }
        }
    }

    private var typed: Int { min(5000, max(0, Int(everyText) ?? 0)) }
    private var changed: Bool { typed != (job?.everyShots ?? 0) }

    private func commit() {
        let value = typed
        Task { await onEvery(value) }
    }

    private func status(_ job: MaintenanceJob) -> String {
        var text = "\(job.sinceShots) shots since"
        if let date = BrewFormat.date(fromEpoch: job.lastEpoch) {
            text += ", last done \(date.formatted(date: .abbreviated, time: .omitted))"
        }
        if job.due {
            text += " - due now"
        } else if job.everyShots > 0 {
            text += " - next in \(max(0, job.everyShots - job.sinceShots)) shots"
        }
        return text
    }
}
