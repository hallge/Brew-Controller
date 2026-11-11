import CoreBluetooth
import Foundation

/// UNTESTED. A CoreBluetooth transport for an Acaia Lunar (2021+) and a SwitchBot Bot.
///
/// It moves bytes and nothing else: notifications go to `CoreSession.onScaleBytes/onBotBytes`
/// (the core's ScaleLink and PusherLink decode them), and what the core wants written comes back as
/// BridgeCommands, drained after every core call. Heartbeats, init writes, the silence watchdog and
/// the press sequence are all in the core (src/core/ScaleLink.h, PusherLink.h); see src/ble/Ble.cpp
/// for the seam this replaces.
///
/// Everything runs on the core's serial queue, which is also the CBCentralManager's queue.
/// Not done in the spike: state restoration, AccessorySetupKit, pairing memory, reconnect back-off.
/// The first scale named like an Acaia and the first Bot found are used: do not switch this on
/// while the deployed board has them.
final class CentralController: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    // Acaia Lunar 2021+ pair (lib/EspressoScaleProtocol/src/Acaia.cpp, kScaleProtocolAcaia).
    private static let scaleNotifyUUID = CBUUID(string: "49535343-1E4D-4BD9-BA61-23C647249616")
    private static let scaleWriteUUID = CBUUID(string: "49535343-8841-43F4-A8D4-ECBE34729BB3")
    // SwitchBot Bot (src/core/SwitchBotProtocol.h).
    private static let botServiceUUID = CBUUID(string: "CBA20D00-224D-11E6-9FB8-0002A5D5C51B")
    private static let botWriteUUID = CBUUID(string: "CBA20002-224D-11E6-9FB8-0002A5D5C51B")
    private static let botNotifyUUID = CBUUID(string: "CBA20003-224D-11E6-9FB8-0002A5D5C51B")
    private static let scaleNamePrefixes = ["LUNAR", "ACAIA", "PYXIS", "PEARL", "CINCO"]

    private let core: CoreSession
    private let queue: DispatchQueue
    private var central: CBCentralManager?

    private var scale: CBPeripheral?
    private var scaleWrite: CBCharacteristic?
    private var scaleNotify: CBCharacteristic?
    private var bot: CBPeripheral?
    private var botWrite: CBCharacteristic?
    private var botNotify: CBCharacteristic?
    private var botLinkUp = false
    private var lastPoll: UInt32 = 0

    var onStatus: ((String) -> Void)?

    init(core: CoreSession, queue: DispatchQueue) {
        self.core = core
        self.queue = queue
        super.init()
    }

    func start() {
        central = CBCentralManager(delegate: self, queue: queue)
        onStatus?("Bluetooth starting")
    }

    func stop() {
        central?.stopScan()
        if let scale { central?.cancelPeripheralConnection(scale) }
        if let bot { central?.cancelPeripheralConnection(bot) }
        core.scaleLink(false, "acaia", CoreSession.uptimeMs())
        core.botLink(false)
        central?.delegate = nil
        central = nil
        scale = nil
        bot = nil
    }

    // MARK: Writes (core queue)

    /// Sends what the core queued. Called after every core call.
    func drain() {
        pollBot()
        while core.pendingCommands() > 0 {
            let command = core.popCommand()
            if command.kind == 0 { break }
            var data = Data()
            for i in 0..<Int(command.length) { data.append(command.byte(UInt8(i))) }
            switch command.kind {
            case Cmd.writeScale:
                write(data, to: scale, characteristic: scaleWrite)
            case Cmd.writeBot:
                write(data, to: bot, characteristic: botWrite)
            default: break
            }
        }
    }

    private func write(_ data: Data, to peripheral: CBPeripheral?, characteristic: CBCharacteristic?) {
        guard let peripheral, let characteristic, peripheral.state == .connected else { return }
        let noResponse = characteristic.properties.contains(.writeWithoutResponse)
        peripheral.writeValue(data, for: characteristic, type: noResponse ? .withoutResponse : .withResponse)
    }

    /// The Bot's link is held only while the core wants it (around a shot), to save its battery.
    private func pollBot() {
        let now = CoreSession.uptimeMs()
        if now &- lastPoll < 250 { return }
        lastPoll = now
        guard let central, let bot else { return }
        let wanted = core.live(now).botWanted
        if wanted && bot.state == .disconnected {
            central.connect(bot)
        } else if !wanted && bot.state == .connected && core.pendingCommands() == 0 {
            central.cancelPeripheralConnection(bot)
        }
    }

    // MARK: CBCentralManagerDelegate

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            central.scanForPeripherals(withServices: nil, options: nil)
            onStatus?("Scanning for a Lunar and a Bot")
        case .unauthorized: onStatus?("Bluetooth is not allowed for this app")
        case .poweredOff: onStatus?("Bluetooth is off")
        default: onStatus?("Bluetooth unavailable")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? ""
        let services = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        let isScale = Self.scaleNamePrefixes.contains { name.uppercased().hasPrefix($0) }
        let isBot = name == "WoHand" || services.contains(Self.botServiceUUID)
        if isScale && scale == nil {
            scale = peripheral
            peripheral.delegate = self
            central.connect(peripheral)
            onStatus?("Found scale \(name), connecting")
        } else if isBot && bot == nil {
            bot = peripheral
            peripheral.delegate = self
            onStatus?("Found Bot \(name.isEmpty ? "(unnamed)" : name): it is connected only around a shot")
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices(nil)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        onStatus?("Connect failed: \(error?.localizedDescription ?? "unknown")")
        if peripheral === scale { central.connect(peripheral) }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        let now = CoreSession.uptimeMs()
        if peripheral === scale {
            scaleWrite = nil
            scaleNotify = nil
            core.scaleLink(false, "acaia", now)
            onStatus?("Scale disconnected")
            central.connect(peripheral)  // keeps trying; the core shows SCALE_DISCONNECTED meanwhile
        } else if peripheral === bot {
            botWrite = nil
            botNotify = nil
            botLinkUp = false
            core.botLink(false)
        }
        drain()
    }

    // MARK: CBPeripheralDelegate

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        for service in peripheral.services ?? [] { peripheral.discoverCharacteristics(nil, for: service) }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        for c in service.characteristics ?? [] {
            if peripheral === scale {
                if c.uuid == Self.scaleNotifyUUID { scaleNotify = c }
                if c.uuid == Self.scaleWriteUUID { scaleWrite = c }
                if let n = scaleNotify, scaleWrite != nil, !n.isNotifying {
                    peripheral.setNotifyValue(true, for: n)
                }
            } else if peripheral === bot {
                if c.uuid == Self.botNotifyUUID { botNotify = c }
                if c.uuid == Self.botWriteUUID { botWrite = c }
                if let n = botNotify, botWrite != nil, !n.isNotifying {
                    peripheral.setNotifyValue(true, for: n)
                }
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard error == nil, characteristic.isNotifying else { return }
        let now = CoreSession.uptimeMs()
        if peripheral === scale {
            // The core queues the scale's init writes (identify, notification request).
            core.scaleLink(true, "acaia", now)
            onStatus?("Scale ready")
        } else if peripheral === bot {
            botLinkUp = true
            core.botLink(true)
        }
        drain()
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard error == nil, let data = characteristic.value, !data.isEmpty else { return }
        let now = CoreSession.uptimeMs()
        data.withUnsafeBytes { raw in
            guard let base = raw.bindMemory(to: UInt8.self).baseAddress else { return }
            if peripheral === scale {
                core.onScaleBytes(base, data.count, now)
            } else if peripheral === bot {
                core.onBotBytes(base, data.count, now)
            }
        }
        drain()
    }
}
