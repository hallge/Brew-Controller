// Swift copies of the bridge's constants (BrewCoreBridge/BridgeTypes.h). They are mirrored, not
// imported, so that one odd import cannot stop the whole app from building.
enum Cmd {
    static let writeScale: UInt8 = 1
    static let writeBot: UInt8 = 2
}

enum Fault {
    static let lostAck: UInt8 = 1
    static let missedStop: UInt8 = 2
    static let scaleSilent: UInt8 = 3
}

enum Probe {
    static let tick: UInt8 = 1
    static let sample: UInt8 = 2
    static let command: UInt8 = 3
    static let state: UInt8 = 4
    static let note: UInt8 = 5
}
