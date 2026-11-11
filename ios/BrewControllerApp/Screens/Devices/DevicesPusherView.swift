// Pusher keys (web: Setup › Pusher keys): the Tuya, MicroBot and SwitchBot-password keys of a bot.
// The keys are write-only: only whether each is set is shown; an empty field keeps what is stored.
import SwiftUI

struct DevicesPusherView: View {
    var store: DevicesStore
    @State private var role = "brew"
    @State private var uuid = ""
    @State private var deviceId = ""
    @State private var localKey = ""
    @State private var variant = 0
    @State private var token = ""
    @State private var password = ""
    @State private var botPassword = ""

    var body: some View {
        Form {
            if let error = store.error {
                Section { Text(error).foregroundStyle(.red).accessibilityIdentifier("devicesPusherError") }
            } else if let notice = store.notice {
                Section { Text(notice).foregroundStyle(.green).accessibilityIdentifier("devicesPusherNotice") }
            }
            Section {
                Picker("For", selection: $role) {
                    Text("Brew bot").tag("brew")
                    Text("Power bot").tag("power")
                    Text("Stop bot").tag("stop")
                }
                .accessibilityIdentifier("devicesPusherRole")
                .onChange(of: role) { _, value in Task { await load(value) } }
                TextField("Tuya uuid", text: $uuid).textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("devicesPusherUuid")
                TextField("Tuya device id", text: $deviceId).textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("devicesPusherDevice")
                SecureField("Tuya local key (6 or more characters)", text: $localKey)
                    .accessibilityIdentifier("devicesPusherKey")
                Picker("Tuya model", selection: $variant) {
                    Text("Fingerbot / Fingerbot Plus").tag(0)
                    Text("CubeTouch").tag(1)
                }
                SecureField("MicroBot token (32 hex digits)", text: $token)
                    .accessibilityIdentifier("devicesPusherToken")
                if role != "brew" {
                    SecureField("SwitchBot password (this bot)", text: $password)
                        .accessibilityIdentifier("devicesPusherPassword")
                } else {
                    SecureField("SwitchBot password (brew bot)", text: $botPassword)
                        .accessibilityIdentifier("devicesPusherBotPassword")
                }
            } footer: {
                Text("Stored: Tuya local key \(store.pusher.tuyaLocalKeySet ? "yes" : "no"), MicroBot token \(store.pusher.microbotTokenSet ? "yes" : "no")"
                     + (role != "brew" ? ", SwitchBot password \(store.pusher.passwordSet ? "yes" : "no")" : "")
                     + ". Empty key fields keep what is stored.")
                    .accessibilityIdentifier("devicesPusherState")
            }
            Section {
                Button("Save keys") {
                    Task {
                        await store.savePusher(uuid: uuid.trimmingCharacters(in: .whitespaces),
                                               deviceId: deviceId.trimmingCharacters(in: .whitespaces),
                                               localKey: localKey, variant: variant,
                                               token: token.trimmingCharacters(in: .whitespaces), password: password)
                        if role == "brew", !botPassword.isEmpty { await store.saveBotPassword(botPassword) }
                        localKey = ""
                        token = ""
                        password = ""
                        botPassword = ""
                    }
                }
                .accessibilityIdentifier("devicesPusherSave")
            }
        }
        .navigationTitle("Pusher keys")
        .task { await load(role) }
    }

    private func load(_ value: String) async {
        await store.loadPusher(value)
        uuid = store.pusher.tuyaUuid
        deviceId = store.pusher.tuyaDeviceId
        variant = store.pusher.tuyaVariant
        localKey = ""
        token = ""
        password = ""
    }
}
