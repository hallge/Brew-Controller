// Home Assistant (web: Setup › Home Assistant, saveHa, testHa). On the phone the controller can reach
// Home Assistant (readings, webhook, MQTT) while the app is awake, but Home Assistant cannot poll it.
import SwiftUI

struct SetupHomeAssistantView: View {
    var store: SetupStore
    @State private var status = SetupHomeAssistant()
    @State private var url = ""
    @State private var token = ""
    @State private var rhEntity = ""
    @State private var tempEntity = ""
    @State private var webhook = ""
    @State private var mqttUrl = ""
    @State private var mqttUser = ""
    @State private var mqttPassword = ""
    @State private var clearToken = false
    @State private var clearWebhook = false
    @State private var clearMqtt = false
    @State private var testText = ""
    @State private var loadedOnce = false

    var body: some View {
        Form {
            SetupMessages(error: store.error, notice: store.notice)
            Section {
                Text("What works on the iPhone: the controller reads the room's humidity and temperature from Home Assistant, sends a finished shot to a webhook, and shows up as a device through MQTT, all while this app is awake.")
                    .font(.footnote)
                Text("What cannot work: Home Assistant polling the controller. The controller is this app, not a device on your network, so integrations that read its web address (the Brew Controller integration's polling, a REST sensor) find nothing. Use MQTT or the webhook instead.")
                    .font(.footnote).foregroundStyle(.orange)
                    .accessibilityIdentifier("setupHaCannot")
            } header: {
                Text("Home Assistant")
            }
            Section {
                TextField("Home Assistant address", text: $url)
                    .textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                    .accessibilityIdentifier("setupHaUrl")
                SecureField(status.tokenSet ? "Long-lived access token (set; blank keeps current)" : "Long-lived access token", text: $token)
                    .accessibilityIdentifier("setupHaToken")
                TextField("Humidity entity", text: $rhEntity).textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("setupHaRh")
                TextField("Temperature entity", text: $tempEntity).textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("setupHaTemp")
                SecureField(status.webhookSet ? "Webhook id for each finished shot (set; blank keeps current)" : "Webhook id for each finished shot", text: $webhook)
                    .accessibilityIdentifier("setupHaWebhook")
            } footer: {
                Text("The room's humidity and temperature are read from Home Assistant every 5 minutes and recorded with each shot (for later study: the grind advice does not use them yet).")
            }
            Section("MQTT") {
                TextField("MQTT broker (empty: off)", text: $mqttUrl)
                    .textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                    .accessibilityIdentifier("setupHaMqtt")
                TextField("MQTT user", text: $mqttUser).textInputAutocapitalization(.never).autocorrectionDisabled()
                SecureField(status.mqttPasswordSet ? "MQTT password (set; blank keeps current)" : "MQTT password", text: $mqttPassword)
                if !status.mqttUrl.isEmpty {
                    Text(status.mqttConnected ? "Connected to the broker." : "Not connected." + (status.mqttProblem.isEmpty ? "" : " \(status.mqttProblem)"))
                        .font(.footnote).foregroundStyle(status.mqttConnected ? Color.green : Color.orange)
                        .accessibilityIdentifier("setupHaMqttState")
                }
                Toggle("Allow Start from Home Assistant", isOn: Binding(
                    get: { store.settings.net.webStartEnabled },
                    set: { value in
                        store.settings.net.webStartEnabled = value
                        Task { await store.saveWebStart(value) }
                    }))
                    .accessibilityIdentifier("setupHaAllowStart")
            }
            Section("Remove") {
                Toggle("Remove the token", isOn: $clearToken)
                Toggle("Remove the webhook", isOn: $clearWebhook)
                Toggle("Remove the MQTT password", isOn: $clearMqtt)
            }
            if !status.problem.isEmpty {
                Section { Text(status.problem).font(.footnote).foregroundStyle(.orange) }
            }
            Section {
                Button("Save") { Task { await save() } }
                    .accessibilityIdentifier("setupHaSave")
                Button("Test the reading") { Task { await test() } }
                    .accessibilityIdentifier("setupHaTest")
                if !testText.isEmpty {
                    Text(testText).font(.footnote).accessibilityIdentifier("setupHaTestResult")
                }
            }
        }
        .navigationTitle("Home Assistant")
        .task { await load() }
    }

    private func load() async {
        if !store.loaded { await store.refresh() }
        guard let value = try? await store.api.get("/api/homeassistant", as: SetupHomeAssistant.self) else { return }
        status = value
        guard !loadedOnce else { return }
        loadedOnce = true
        url = value.url
        rhEntity = value.rhEntity
        tempEntity = value.tempEntity
        mqttUrl = value.mqttUrl
        mqttUser = value.mqttUser
    }

    private func save() async {
        var body: [String: BrewJSON] = [
            "url": .string(url.trimmingCharacters(in: .whitespaces)),
            "rh_entity": .string(rhEntity.trimmingCharacters(in: .whitespaces)),
            "temp_entity": .string(tempEntity.trimmingCharacters(in: .whitespaces)),
            "mqtt_url": .string(mqttUrl.trimmingCharacters(in: .whitespaces)),
            "mqtt_user": .string(mqttUser.trimmingCharacters(in: .whitespaces)),
            "clear_token": .bool(clearToken),
            "clear_webhook": .bool(clearWebhook),
            "clear_mqtt_password": .bool(clearMqtt),
        ]
        if !token.isEmpty { body["token"] = .string(token) }
        if !webhook.isEmpty { body["webhook_id"] = .string(webhook) }
        if !mqttPassword.isEmpty { body["mqtt_password"] = .string(mqttPassword) }
        let ok = await store.perform("Saved", refresh: false) {
            try await store.api.post("/api/homeassistant", json: body)
        }
        if ok {
            token = ""
            webhook = ""
            mqttPassword = ""
            clearToken = false
            clearWebhook = false
            clearMqtt = false
            if let value = try? await store.api.get("/api/homeassistant", as: SetupHomeAssistant.self) { status = value }
        }
    }

    private func test() async {
        testText = "Reading…"
        do {
            let result = try await store.api.get("/api/ambient/test", as: SetupAmbientTest.self)
            if result.ok {
                testText = "Read: " + [result.rhPct.map { "\(BrewFormat.decimal($0, 0)) % humidity" },
                                       result.tempC.map { BrewFormat.celsius($0) }].compactMap { $0 }.joined(separator: ", ")
            } else {
                testText = result.problem.isEmpty ? "Not read." : result.problem
            }
        } catch let failure as BrewAPIError {
            testText = failure.message
        } catch {
            testText = String(describing: error)
        }
    }
}
