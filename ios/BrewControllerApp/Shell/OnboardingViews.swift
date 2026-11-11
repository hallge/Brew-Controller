// The first run, on top of `Onboarding` (Core/Onboarding.swift): the safety notice, demo or real
// devices, the Bluetooth step (real devices only) and the diagnostics question.
// The safety wording is condensed from docs/SAFETY.md, which is still a DRAFT for the owner and a lawyer.
import SwiftUI
import UIKit

struct BrewOnboardingFlow: View {
    @State private var onboarding = Onboarding.shared

    var body: some View {
        ZStack {
            brewPal.bg.ignoresSafeArea()
            switch onboarding.step {
            case .safetyNotice: BrewSafetyNoticeView()
            case .chooseMode: BrewChooseModeView()
            case .bluetooth: BrewBluetoothStepView()
            case .diagnostics, .done: BrewDiagnosticsStepView()
            }
        }
        .accessibilityIdentifier("onboarding")
    }
}

private struct BrewOnboardingPage<Content: View>: View {
    var title: String
    @ViewBuilder var content: Content

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                Text(title).font(.largeTitle.weight(.bold)).foregroundStyle(brewPal.text)
                content
            }
            .padding(24)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

/// The three paragraphs of the notice, for the first run and for reading it again later.
struct BrewSafetyText: View {
    var body: some View {
        Group {
            Text("Brew Controller is a hobby project, not a certified appliance. It presses a button on your espresso machine to stop a shot at a target weight. It has not been tested by any safety body and comes without warranty.")
            Text("It is an aid to stopping at a weight, not a safety device. Be present while a shot runs: if the scale or the button pusher is lost, a machine that was started keeps running until you stop it by hand.")
            Text("Check the first shots by hand before leaving it to itself.")
        }
        .foregroundStyle(brewPal.text)
    }
}

struct BrewSafetyNoticeView: View {
    var body: some View {
        BrewOnboardingPage(title: "Before you brew") {
            BrewSafetyText()
            Button("I understand") { Onboarding.shared.acceptSafetyNotice() }
                .buttonStyle(BrewKeyStyle(kind: .ok))
                .accessibilityIdentifier("acceptSafetyButton")
        }
        .accessibilityIdentifier("safetyNotice")
    }
}

/// The notice read again from More: no button to accept, just Done.
struct BrewSafetyNoticeSheet: View {
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ZStack {
                brewPal.bg.ignoresSafeArea()
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        BrewSafetyText()
                    }
                    .padding(24)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .navigationTitle("Before you brew")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }.accessibilityIdentifier("safetyDone")
                }
            }
        }
        .accessibilityIdentifier("safetySheet")
    }
}

struct BrewChooseModeView: View {
    var body: some View {
        BrewOnboardingPage(title: "Which devices?") {
            Text("Try the app with a simulated scale, button pusher and machine, or use your own devices.")
                .foregroundStyle(brewPal.text)
            Button("TRY THE DEMO") { Onboarding.shared.chooseMode(.demo) }
                .buttonStyle(BrewKeyStyle(kind: .accent))
                .accessibilityIdentifier("modeDemoButton")
            Button("USE MY DEVICES") { Onboarding.shared.chooseMode(.real) }
                .buttonStyle(BrewKeyStyle(kind: .panel))
                .accessibilityIdentifier("modeRealButton")
        }
        .accessibilityIdentifier("chooseMode")
    }
}

struct BrewBluetoothStepView: View {
    @State private var onboarding = Onboarding.shared

    var body: some View {
        BrewOnboardingPage(title: "Bluetooth") {
            Text("The app talks to your scale and button pusher over Bluetooth. iOS asks for permission the first time.")
                .foregroundStyle(brewPal.text)
            if onboarding.bluetooth.needsSettings {
                Text("Bluetooth is turned off for this app. Turn it on in Settings.")
                    .foregroundStyle(brewPal.bad)
                Button("OPEN SETTINGS") {
                    if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
                }
                .buttonStyle(BrewKeyStyle(kind: .panel))
            }
            Button("CONTINUE") { Onboarding.shared.bluetoothStepFinished() }
                .buttonStyle(BrewKeyStyle(kind: .accent))
                .accessibilityIdentifier("bluetoothContinueButton")
        }
        .task { onboarding.refreshBluetooth() }
        .accessibilityIdentifier("bluetoothStep")
    }
}

struct BrewDiagnosticsStepView: View {
    @State private var onboarding = Onboarding.shared
    @State private var sending = false

    var body: some View {
        BrewOnboardingPage(title: "Diagnostics") {
            Text("Share anonymous diagnostic reports to help find problems? You can change this later.")
                .foregroundStyle(brewPal.text)
            if let error = onboarding.consentError {
                Text(error.message).foregroundStyle(brewPal.bad)
            }
            Button("SHARE") { answer(true) }
                .buttonStyle(BrewKeyStyle(kind: .accent))
                .disabled(sending)
                .accessibilityIdentifier("diagnosticsOnButton")
            Button("NOT NOW") { answer(false) }
                .buttonStyle(BrewKeyStyle(kind: .panel))
                .disabled(sending)
                .accessibilityIdentifier("diagnosticsOffButton")
        }
        .accessibilityIdentifier("diagnosticsStep")
    }

    private func answer(_ consent: Bool) {
        sending = true
        Task {
            await Onboarding.shared.answerDiagnostics(consent)
            sending = false
        }
    }
}
