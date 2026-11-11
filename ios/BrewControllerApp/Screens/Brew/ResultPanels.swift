// The board's RESULT, FAULT and GRINDREC panels. The words are the firmware's, from /api/screen
// (src/ui/ScreenText.h): the verdict line, times, flow, drip, the grind verdict, every fault title and
// message, and the grind questions. Done, Press and the key answers are direct commands.
import SwiftUI

// MARK: - Result

struct BrewResultPanel: View {
    var screen: ScreenSnapshot
    var onGrinder: () -> Void
    @State private var acked = false

    private var model: BrewModel { BrewModel.shared }

    var body: some View {
        let result = screen.result ?? ScreenResult()
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                BrewCard {
                    VStack(alignment: .leading, spacing: 8) {
                        if let top = result.top {
                            Text(top).font(.title2.weight(.semibold))
                                .foregroundStyle(brewPal.color(forTone: result.topTone))
                                .accessibilityIdentifier("resultTop")
                        }
                        if let weight = result.weight {
                            Text(weight).font(.system(size: 52, weight: .semibold, design: .rounded))
                                .foregroundStyle(brewPal.text).minimumScaleFactor(0.5)
                                .accessibilityIdentifier("resultWeight")
                        }
                        if let line1 = result.line1 {
                            Text(line1).font(.body).foregroundStyle(brewPal.text)
                                .accessibilityIdentifier("resultTimes")
                        }
                        if let detail = result.line2.first {
                            // The firmware gives the wordings longest first; the phone has room for the longest.
                            Text(detail).font(.body).foregroundStyle(brewPal.dim)
                                .accessibilityIdentifier("resultDetail")
                        }
                    }
                }
                if result.review {
                    BrewCard {
                        Text(result.reviewText ?? result.reviewTextShort ?? "")
                            .foregroundStyle(brewPal.accent)
                            .accessibilityIdentifier("resultReview")
                    }
                } else if let grind = result.grind {
                    BrewCard {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(grind).font(.title3.weight(.semibold))
                                .foregroundStyle(brewPal.color(forTone: result.grindTone))
                                .accessibilityIdentifier("resultGrind")
                            if let sub = result.grindSub, !sub.isEmpty {
                                Text(sub).foregroundStyle(brewPal.dim)
                            }
                        }
                    }
                }
                keys(result)
            }
            .padding(16)
        }
        .accessibilityIdentifier("resultPanel")
        // The used cup taken off and a fresh one put on: back to the home screen without a tap, unless
        // the result is still asking whether the shot counts for the grind.
        .onChange(of: result.autoAck, initial: true) { _, auto in
            if auto && !acked {
                acked = true
                LiveModel.shared.send(.acknowledge)
            }
        }
    }

    @ViewBuilder private func keys(_ result: ScreenResult) -> some View {
        if result.review {
            HStack(spacing: 10) {
                Button { vote(result, ignore: true) } label: { Text("IGNORE\nSHOT") }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .accessibilityIdentifier("ignoreButton")
                Button { vote(result, ignore: false) } label: { Text("COUNT FOR\nGRIND") }
                    .buttonStyle(BrewKeyStyle(kind: .accent))
                    .accessibilityIdentifier("countButton")
            }
        } else if result.grindChange {
            HStack(spacing: 10) {
                Button("READY") { LiveModel.shared.send(.acknowledge) }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .accessibilityIdentifier("readyButton")
                Button("GRINDER") {
                    // The shot is done with, and the grind is next.
                    LiveModel.shared.send(.acknowledge)
                    onGrinder()
                }
                .buttonStyle(BrewKeyStyle(kind: .accent))
                .accessibilityIdentifier("grinderButton")
            }
        } else {
            Button("READY") { LiveModel.shared.send(.acknowledge) }
                .buttonStyle(BrewKeyStyle(kind: .panel))
                .accessibilityIdentifier("readyButton")
        }
    }

    private func vote(_ result: ScreenResult, ignore: Bool) {
        guard let id = result.shotId else { return }
        Task { await model.vote(shotId: id, ignore: ignore) }
    }
}

// MARK: - Fault

struct BrewFaultPanel: View {
    var screen: ScreenSnapshot

    var body: some View {
        let fault = screen.fault ?? ScreenFault()
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                BrewCard {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(fault.title).font(.title2.weight(.bold)).foregroundStyle(brewPal.bad)
                            .accessibilityIdentifier("faultTitle")
                        Text(fault.message).foregroundStyle(brewPal.text)
                            .accessibilityIdentifier("faultMessage")
                    }
                }
                if fault.offerPress {
                    // One manual press of the brew bot, when the start or stop was not confirmed.
                    Button("PRESS BUTTON") { BrewModel.shared.manualPress() }
                        .buttonStyle(BrewKeyStyle(kind: .accent))
                        .accessibilityIdentifier("pressButton")
                }
                Button("OK") { LiveModel.shared.send(.acknowledge) }
                    .buttonStyle(BrewKeyStyle(kind: .panel))
                    .accessibilityIdentifier("faultOkButton")
            }
            .padding(16)
        }
        .accessibilityIdentifier("faultPanel")
    }
}

// MARK: - Grind prompt

/// NEW GRIND ADVICE, SET THE GRIND and GRIND REMINDER: one panel, three kinds.
struct BrewGrindPromptPanel: View {
    var screen: ScreenSnapshot
    /// SET on the no-grind question opens the editor on the grind, at this setting.
    var onEditGrind: (Int) -> Void

    var body: some View {
        let prompt = screen.grindPrompt ?? ScreenGrindPrompt()
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                BrewCard {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(prompt.title).font(.headline).foregroundStyle(brewPal.dim)
                            .accessibilityIdentifier("grindPromptTitle")
                        Text(prompt.setting).font(.system(size: 40, weight: .semibold, design: .rounded))
                            .foregroundStyle(brewPal.accent).minimumScaleFactor(0.5)
                            .accessibilityIdentifier("grindPromptSetting")
                        Text(prompt.why).foregroundStyle(brewPal.text)
                            .accessibilityIdentifier("grindPromptWhy")
                    }
                }
                HStack(spacing: 10) {
                    Button { answer(prompt, use: true) } label: { Text(prompt.use) }
                        .buttonStyle(BrewKeyStyle(kind: .accent))
                        .accessibilityIdentifier("grindUseButton")
                    Button { answer(prompt, use: false) } label: { Text(prompt.keep) }
                        .buttonStyle(BrewKeyStyle(kind: .panel))
                        .accessibilityIdentifier("grindKeepButton")
                }
            }
            .padding(16)
        }
        .accessibilityIdentifier("grindPromptPanel")
    }

    private func answer(_ prompt: ScreenGrindPrompt, use: Bool) {
        Task {
            let editUm = await BrewModel.shared.answerGrind(use: use)
            // The no-grind question's SET opens the editor on the grind, as the board's does.
            if use, prompt.kind == "no_grind" { onEditGrind(editUm ?? prompt.useUm) }
        }
    }
}
