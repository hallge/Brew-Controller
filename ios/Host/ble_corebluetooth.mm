// CoreBluetooth backend for the firmware's BleBackend (emu/src/ble_backend.h).
// Notes: ios/Host/BLE.md. Written without the iOS SDK at hand: first compiled on
// Codemagic.
//
// Layout of this file:
//   1. bbwcb:: pure C++ helpers (UUID text, SwitchBot MAC, address book, error
//      codes). No platform headers, so they are unit-tested on a PC by compiling
//      this file with -DBBW_CB_HELPERS_ONLY.
//   2. BBWCentral: the Objective-C++ class that owns the CBCentralManager.
//   3. The BleBackend adapter and the plain-C entry points.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ble_backend.h"
#include "ble_corebluetooth.h"

// ============================================================ 1. helpers ====
namespace bbwcb {

// 2, 4 or 16 big-endian UUID bytes (CBUUID.data) as the lower-case text NimBLE
// uses: 128-bit "8-4-4-4-12", the short forms expanded with the Bluetooth base UUID.
inline std::string expandUuid(const uint8_t *b, size_t n) {
  static const char *kBase = "-0000-1000-8000-00805f9b34fb";
  char buf[64];
  if (n == 2) {
    snprintf(buf, sizeof(buf), "0000%02x%02x%s", b[0], b[1], kBase);
  } else if (n == 4) {
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%s", b[0], b[1], b[2], b[3], kBase);
  } else if (n == 16) {
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
             b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14],
             b[15]);
  } else {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
      snprintf(buf, sizeof(buf), "%02x", b[i]);
      out += buf;
    }
    return out;
  }
  return buf;
}

// SwitchBot: the advertisement carries Woan's manufacturer data, company id 0x0969
// (little endian: 69 09) followed by the device's own MAC, in reading order.
// src/ble/Ble.cpp (onAdvertisement) formats bytes 2..7 as "%02x:..." and compares
// them with the address it was given, so that is the address to report.
inline bool switchBotMac(const std::string &manufacturerData, std::string *mac) {
  const std::string &m = manufacturerData;
  if (m.size() < 8 || static_cast<uint8_t>(m[0]) != 0x69 || static_cast<uint8_t>(m[1]) != 0x09) {
    return false;
  }
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", static_cast<uint8_t>(m[2]),
           static_cast<uint8_t>(m[3]), static_cast<uint8_t>(m[4]), static_cast<uint8_t>(m[5]),
           static_cast<uint8_t>(m[6]), static_cast<uint8_t>(m[7]));
  *mac = buf;
  return true;
}

// First 6 bytes of the 16-byte CBPeripheral.identifier, top two bits set: a
// static random address.
inline std::string derivedAddress(const uint8_t id[16]) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", static_cast<unsigned>(id[0] | 0xC0),
           id[1], id[2], id[3], id[4], id[5]);
  return buf;
}

// CBError codes (CBErrorDomain) to the codes Ble.cpp's connectFailText knows:
// 0 = own timeout / no specific code, 13 = connection timeout, 0x208 = link lost,
// 0x23e = the link could not be set up.
inline int mapCbError(bool cbErrorDomain, long code) {
  if (!cbErrorDomain) return 0x23e;
  switch (code) {
    case 5: return 0;       // CBErrorOperationCancelled (our own cancel)
    case 6: return 13;      // CBErrorConnectionTimeout
    case 15: return 13;     // CBErrorEncryptionTimedOut
    case 7: return 0x208;   // CBErrorPeripheralDisconnected
    default: return 0x23e;  // failed, limit reached, unknown device, ...
  }
}

// Address <-> CBPeripheral.identifier, kept in <dataDir>/host/ble_ids.json.
// Flat JSON object. "aa:bb:cc:dd:ee:ff": "<UUID>" is a derived address;
// "<UUID>|mac" marks a real MAC read from SwitchBot manufacturer data (it wins over a
// derived one). Keys starting with "_" hold the paired scale. Used on the CoreBluetooth
// queue only.
class AddressBook {
 public:
  void setPath(const std::string &path) { path_ = path; }

  // The address to report for a peripheral. `mac` is empty unless this
  // advertisement carried a SwitchBot MAC.
  std::string addressFor(const std::string &id, const std::string &mac, bool *changed) {
    auto it = byId_.find(id);
    if (!mac.empty()) {
      if (it != byId_.end() && it->second.address == mac && it->second.real) return mac;
      if (it != byId_.end()) dropAddress(it->second.address, id);
      auto other = byAddr_.find(mac);
      if (other != byAddr_.end() && other->second != id) byId_.erase(other->second);
      byId_[id] = Entry{mac, true};
      byAddr_[mac] = id;
      if (changed) *changed = true;
      return mac;
    }
    if (it != byId_.end()) return it->second.address;
    uint8_t bytes[16];
    if (!parseUuid(id, bytes)) std::memset(bytes, 0, sizeof(bytes));
    std::string derived = derivedAddress(bytes);
    byId_[id] = Entry{derived, false};
    byAddr_[derived] = id;
    if (changed) *changed = true;
    return derived;
  }

  // The identifier an address maps to; empty if unknown.
  std::string idFor(const std::string &address) const {
    auto it = byAddr_.find(address);
    return it == byAddr_.end() ? std::string() : it->second;
  }
  std::string addressOfId(const std::string &id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? std::string() : it->second.address;
  }

  std::string pairedScaleId, pairedScaleName;

  bool save() const {
    if (path_.empty()) return true;
    std::string out = "{";
    bool first = true;
    auto put = [&](const std::string &k, const std::string &v) {
      if (!first) out += ",";
      first = false;
      out += "\n  \"" + escape(k) + "\": \"" + escape(v) + "\"";
    };
    for (const auto &e : byId_) put(e.second.address, e.first + (e.second.real ? "|mac" : ""));
    if (!pairedScaleId.empty()) {
      put("_scale", pairedScaleId);
      put("_scale_name", pairedScaleName);
    }
    out += "\n}\n";
    const std::string tmp = path_ + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    if (!ok) return false;
    return std::rename(tmp.c_str(), path_.c_str()) == 0;  // replaces on POSIX
  }

  void load() {
    if (path_.empty()) return;
    FILE *f = std::fopen(path_.c_str(), "rb");
    if (!f) return;
    std::string text;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    parse(text);
  }

  // Public for the unit test.
  void parse(const std::string &text) {
    size_t pos = 0;
    std::string key, value;
    while (nextString(text, &pos, &key)) {
      if (!nextString(text, &pos, &value)) break;
      if (key == "_scale") {
        pairedScaleId = value;
      } else if (key == "_scale_name") {
        pairedScaleName = value;
      } else if (key.size() == 17) {
        bool real = false;
        const size_t bar = value.find('|');
        if (bar != std::string::npos) {
          real = value.compare(bar, std::string::npos, "|mac") == 0;
          value.resize(bar);
        }
        byId_[value] = Entry{key, real};
        byAddr_[key] = value;
      }
    }
  }

 private:
  struct Entry {
    std::string address;
    bool real = false;
  };

  static bool parseUuid(const std::string &text, uint8_t out[16]) {
    int count = 0;
    int high = -1;
    for (char c : text) {
      int v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      else continue;  // dashes
      if (high < 0) {
        high = v;
      } else {
        if (count >= 16) return false;
        out[count++] = static_cast<uint8_t>(high * 16 + v);
        high = -1;
      }
    }
    return count == 16 && high < 0;
  }

  void dropAddress(const std::string &address, const std::string &id) {
    auto it = byAddr_.find(address);
    if (it != byAddr_.end() && it->second == id) byAddr_.erase(it);
  }

  static std::string escape(const std::string &s) {
    std::string out;
    for (char c : s) {
      if (c == '"' || c == '\\') out += '\\';
      if (static_cast<unsigned char>(c) < 0x20) continue;
      out += c;
    }
    return out;
  }

  // Next "..." string at or after *pos (skipping braces, colons, commas).
  static bool nextString(const std::string &t, size_t *pos, std::string *out) {
    size_t i = *pos;
    while (i < t.size() && t[i] != '"') ++i;
    if (i >= t.size()) return false;
    ++i;
    out->clear();
    while (i < t.size() && t[i] != '"') {
      if (t[i] == '\\' && i + 1 < t.size()) ++i;
      out->push_back(t[i++]);
    }
    if (i >= t.size()) return false;
    *pos = i + 1;
    return true;
  }

  std::string path_;
  std::map<std::string, Entry> byId_;      // identifier -> address
  std::map<std::string, std::string> byAddr_;  // address -> identifier
};

// Characteristic UUIDs that only scales use, to recognise "the paired scale": the
// legacy Acaia 2a80, the 49535343-... pairs of the Lunar 2021 and Pyxis, and the
// other scale makes' own UUIDs from lib/ScaleWire. Shared UUIDs (ffe1, fff1, ...)
// are left out because pushers use them too.
inline bool isScaleCharacteristic(const std::string &uuid) {
  static const char *kScale[] = {
      "00002a80-0000-1000-8000-00805f9b34fb", "49535343-1e4d-4bd9-ba61-23c647249616",
      "49535343-8841-43f4-a8d4-ecbe34729bb3", "4f9a45ba-8e1b-4e07-e157-0814d393b968",
      "ad736c5f-bbc9-1f96-d304-cb5d5f41e160"};
  for (const char *s : kScale) {
    if (uuid == s) return true;
  }
  return false;
}

}  // namespace bbwcb

#ifndef BBW_CB_HELPERS_ONLY
// ===================================================== 2. BBWCentral ========
#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>

namespace {

using Clock = std::chrono::steady_clock;

const uint32_t kSubscribeTimeoutMs = 5000;
const uint32_t kWriteTimeoutMs = 8000;
const uint32_t kWriteNoResponseWaitMs = 100;
const double kStandingGraceS = 15.0;    // how long a standing-connected scale waits for the firmware
const double kStandingHoldoffS = 60.0;  // after giving up on it
const double kStandingRearmS = 5.0;     // after a normal link end
const size_t kMaxKnownPeripherals = 400;
const char *kQueueLabel = "bbw.corebluetooth";

char kQueueKey;

// One blocking request from a firmware thread, completed on the CoreBluetooth queue.
struct Waiter {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  bool ok = false;
};

void complete(const std::shared_ptr<Waiter> &w, bool ok) {
  {
    std::lock_guard<std::mutex> lock(w->m);
    w->done = true;
    w->ok = ok;
  }
  w->cv.notify_all();
}

typedef std::pair<BleLink, uintptr_t> CharKey;  // link, CBCharacteristic address

struct PendingWrite {  // a write without response waiting for canSendWriteWithoutResponse
  uint64_t seq;
  CBCharacteristic *characteristic;
  NSData *data;
  std::shared_ptr<Waiter> waiter;
};

}  // namespace

// What a link is on the CoreBluetooth side.
@interface BBWLinkRec : NSObject
@property(nonatomic, assign) BleLink linkId;
@property(nonatomic, strong) CBPeripheral *peripheral;
@end
@implementation BBWLinkRec
@end

namespace {

struct ConnectOp {
  std::string address;
  std::string idStr;
  // The caller's wait:
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  bool abandoned = false;  // the caller gave up (timeout); never register a link
  bool ok = false;
  int err = 0;
  BleLink link = 0;
  std::vector<BleServiceInfo> services;
  // CoreBluetooth queue only:
  CBPeripheral *peripheral = nil;
  int pendingChars = 0;
  bool resolving = false;  // waiting for a scan to map the address
};

}  // namespace

@interface BBWCentral : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
- (instancetype)initWithDataDir:(const char *)dataDir restoreId:(const char *)restoreId;
- (void)setCallbacks:(const BleCallbacks &)callbacks;
- (void)start;  // create the central, wait until its state is known
- (bool)scanStart;
- (bool)scanStop;
- (bool)isScanning;
- (bool)connect:(const std::string &)address
        timeoutMs:(uint32_t)timeoutMs
             link:(BleLink *)link
         services:(std::vector<BleServiceInfo> *)services
        lastError:(int *)lastError;
- (void)disconnect:(BleLink)link;
- (bool)isConnected:(BleLink)link;
- (bool)subscribe:(BleLink)link characteristic:(const BleCharRef &)ref;
- (bool)write:(BleLink)link
    characteristic:(const BleCharRef &)ref
              data:(const uint8_t *)data
            length:(size_t)length
          response:(bool)response;
- (int)bluetoothState;
@end

@implementation BBWCentral {
  dispatch_queue_t _q;
  CBCentralManager *_central;
  std::string _restoreId;

  // Shared with firmware threads; _mu is never held while calling a callback or
  // while waiting on the CoreBluetooth queue.
  std::mutex _mu;
  std::condition_variable _stateCv;
  BleCallbacks _callbacks;
  std::set<BleLink> _liveLinks;
  std::atomic<int> _state;           // BbwBluetoothState
  std::atomic<bool> _scanFlag;       // firmware wants adverts
  std::atomic<uint64_t> _nextLink;

  // CoreBluetooth queue only:
  bbwcb::AddressBook _book;
  std::map<std::string, std::string> _names;  // identifier -> last local name heard
  NSMutableDictionary<NSUUID *, CBPeripheral *> *_peripherals;
  NSMutableDictionary<NSNumber *, BBWLinkRec *> *_links;              // by link id
  NSMutableDictionary<NSUUID *, BBWLinkRec *> *_linkByPeripheral;
  std::map<std::string, std::shared_ptr<ConnectOp>> _connecting;      // by identifier
  std::vector<std::shared_ptr<ConnectOp>> _resolving;
  std::map<CharKey, std::deque<std::shared_ptr<Waiter>>> _subWaiters;
  std::map<CharKey, std::deque<std::shared_ptr<Waiter>>> _writeWaiters;
  std::map<BleLink, std::deque<PendingWrite>> _pendingWrites;
  uint64_t _writeSeq;
  bool _cbScanning;
  // The standing connection to the paired scale.
  CBPeripheral *_standing;
  bool _standingConnected;  // connected, not yet taken over by a firmware connect()
  Clock::time_point _standingSince;
  Clock::time_point _standingHoldoff;
  bool _standingTicking;
}

- (instancetype)initWithDataDir:(const char *)dataDir restoreId:(const char *)restoreId {
  if ((self = [super init])) {
    _q = dispatch_queue_create(kQueueLabel, DISPATCH_QUEUE_SERIAL);
    dispatch_queue_set_specific(_q, &kQueueKey, (__bridge void *)self, NULL);
    _state = BBW_BT_UNKNOWN;
    _scanFlag = false;
    _nextLink = 0;
    _writeSeq = 0;
    _cbScanning = false;
    _standingConnected = false;
    _standingTicking = false;
    _standingHoldoff = Clock::now();
    _peripherals = [NSMutableDictionary dictionary];
    _links = [NSMutableDictionary dictionary];
    _linkByPeripheral = [NSMutableDictionary dictionary];
    if (restoreId && restoreId[0]) _restoreId = restoreId;
    if (dataDir && dataDir[0]) {
      NSString *dir = [[NSString stringWithUTF8String:dataDir] stringByAppendingPathComponent:@"host"];
      [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:nil];
      _book.setPath([[dir stringByAppendingPathComponent:@"ble_ids.json"] UTF8String]);
      _book.load();
    }
  }
  return self;
}

// ---------------------------------------------------------------- helpers ----

- (bool)onQueue {
  return dispatch_get_specific(&kQueueKey) == (__bridge void *)self;
}

// Runs inline on the CoreBluetooth queue, otherwise posts.
- (void)post:(dispatch_block_t)block {
  if ([self onQueue]) block();
  else dispatch_async(_q, block);
}

- (void)setCallbacks:(const BleCallbacks &)callbacks {
  std::lock_guard<std::mutex> lock(_mu);
  _callbacks = callbacks;
}

static std::string uuidText(CBUUID *uuid) {
  NSData *d = uuid.data;
  return bbwcb::expandUuid(static_cast<const uint8_t *>(d.bytes), d.length);
}

static std::string toStd(NSData *d) {
  return d ? std::string(static_cast<const char *>(d.bytes), d.length) : std::string();
}

static std::string idText(CBPeripheral *p) { return std::string([p.identifier.UUIDString UTF8String]); }

- (std::string)addressForPeripheral:(CBPeripheral *)p mac:(const std::string &)mac {
  bool changed = false;
  std::string address = _book.addressFor(idText(p), mac, &changed);
  if (changed) _book.save();
  return address;
}

// Delivers an advertisement to the firmware (outside every lock of ours).
- (void)emitAdvert:(const BleAdvert &)advert {
  std::function<void(const BleAdvert &)> f;
  {
    std::lock_guard<std::mutex> lock(_mu);
    f = _callbacks.onAdvert;
  }
  if (f) f(advert);
}

- (void)emitDisconnect:(BleLink)link {
  std::function<void(BleLink)> f;
  {
    std::lock_guard<std::mutex> lock(_mu);
    f = _callbacks.onDisconnect;
  }
  if (f) f(link);
}

- (CBPeripheral *)peripheralForId:(const std::string &)idStr {
  if (idStr.empty()) return nil;
  NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:[NSString stringWithUTF8String:idStr.c_str()]];
  if (!uuid) return nil;
  CBPeripheral *p = _peripherals[uuid];
  if (!p && _central) {
    p = [[_central retrievePeripheralsWithIdentifiers:@[ uuid ]] firstObject];
    if (p) _peripherals[uuid] = p;
  }
  return p;
}

// ------------------------------------------------------------ lifecycle ----

- (void)start {
  const bool inline_ = [self onQueue];
  [self post:^{
    if (!self->_central) {
      NSMutableDictionary *options = [NSMutableDictionary dictionary];
      options[CBCentralManagerOptionShowPowerAlertKey] = @YES;
      if (!self->_restoreId.empty()) {
        options[CBCentralManagerOptionRestoreIdentifierKey] =
            [NSString stringWithUTF8String:self->_restoreId.c_str()];
      }
      self->_central = [[CBCentralManager alloc] initWithDelegate:self queue:self->_q options:options];
    }
  }];
  if (inline_) return;
  // "Returns when the radio is usable": wait for the first state, but not forever;
  // the permission prompt may be up.
  std::unique_lock<std::mutex> lock(_mu);
  _stateCv.wait_for(lock, std::chrono::milliseconds(2500),
                    [&] { return _state.load() != BBW_BT_UNKNOWN; });
}

- (int)bluetoothState {
  return _state.load();
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
  int state = BBW_BT_UNKNOWN;
  switch (central.state) {
    case CBManagerStatePoweredOn: state = BBW_BT_POWERED_ON; break;
    case CBManagerStatePoweredOff: state = BBW_BT_POWERED_OFF; break;
    case CBManagerStateUnauthorized: state = BBW_BT_UNAUTHORIZED; break;
    case CBManagerStateUnsupported: state = BBW_BT_UNSUPPORTED; break;
    default: state = BBW_BT_UNKNOWN; break;  // resetting, unknown
  }
  _state = state;
  _stateCv.notify_all();
  if (state == BBW_BT_POWERED_ON) {
    [self updateScan];
    [self armStanding];
    return;
  }
  if (state == BBW_BT_UNKNOWN) return;
  // Radio unusable: every link is gone and every pending attempt fails. CoreBluetooth
  // does not promise a didDisconnect for each, so end them here (once each).
  _cbScanning = false;
  _standingConnected = false;
  _standing = nil;
  std::vector<std::shared_ptr<ConnectOp>> ops;
  for (auto &e : _connecting) ops.push_back(e.second);
  for (auto &op : _resolving) ops.push_back(op);
  for (auto &op : ops) [self failOp:op code:0x23e cancel:NO];
  NSArray<BBWLinkRec *> *recs = [_links allValues];
  for (BBWLinkRec *rec in recs) [self endLink:rec];
}

- (void)centralManager:(CBCentralManager *)central willRestoreState:(NSDictionary<NSString *, id> *)dict {
  // The app was relaunched by the system for a Bluetooth event. The firmware has no
  // links, so a restored connection is only useful if it is the paired scale (it is
  // then the standing connection); anything else is dropped.
  NSArray<CBPeripheral *> *restored = dict[CBCentralManagerRestoredStatePeripheralsKey];
  for (CBPeripheral *p in restored) {
    _peripherals[p.identifier] = p;
    p.delegate = self;
    const bool isPaired = !_book.pairedScaleId.empty() && idText(p) == _book.pairedScaleId;
    if (isPaired) {
      _standing = p;
      if (p.state == CBPeripheralStateConnected) [self standingConnected:p];
    } else if (p.state != CBPeripheralStateDisconnected) {
      [central cancelPeripheralConnection:p];
    }
  }
}

// ------------------------------------------------------------------ scan ----

- (bool)scanStart {
  const int state = _state.load();
  [self post:^{
    self->_scanFlag = true;
    [self updateScan];
    [self deliverStandingAdvert];
  }];
  _scanFlag = true;  // visible to isScanning at once
  return state == BBW_BT_POWERED_ON || state == BBW_BT_UNKNOWN;
}

- (bool)scanStop {
  _scanFlag = false;
  [self post:^{
    self->_scanFlag = false;
    [self updateScan];
  }];
  return true;
}

- (bool)isScanning {
  return _scanFlag.load();
}

// Starts or stops the CoreBluetooth scan: wanted by the firmware, or while a
// connect() is waiting for its address to show up. Queue only.
- (void)updateScan {
  if (!_central) return;
  const bool want = (_scanFlag.load() || !_resolving.empty()) && _state.load() == BBW_BT_POWERED_ON;
  if (want && !_cbScanning) {
    [_central scanForPeripheralsWithServices:nil
                                     options:@{CBCentralManagerScanOptionAllowDuplicatesKey : @YES}];
    _cbScanning = true;
  } else if (!want && _cbScanning) {
    [_central stopScan];
    _cbScanning = false;
  }
}

- (void)centralManager:(CBCentralManager *)central
    didDiscoverPeripheral:(CBPeripheral *)peripheral
        advertisementData:(NSDictionary<NSString *, id> *)adv
                     RSSI:(NSNumber *)RSSI {
  if (_peripherals.count > kMaxKnownPeripherals) {
    // Forget the ones nothing needs; retrievePeripheralsWithIdentifiers brings them back.
    NSMutableDictionary *keep = [NSMutableDictionary dictionary];
    for (NSUUID *key in _peripherals) {
      CBPeripheral *p = _peripherals[key];
      if (p.state != CBPeripheralStateDisconnected || _linkByPeripheral[key] || p == _standing) {
        keep[key] = p;
      }
    }
    _peripherals = keep;
  }
  _peripherals[peripheral.identifier] = peripheral;

  BleAdvert advert;
  NSData *manufacturer = adv[CBAdvertisementDataManufacturerDataKey];
  advert.manufacturerData = toStd(manufacturer);
  std::string mac;
  const bool isSwitchBot = bbwcb::switchBotMac(advert.manufacturerData, &mac);
  advert.address = [self addressForPeripheral:peripheral mac:isSwitchBot ? mac : std::string()];
  advert.addressType = isSwitchBot ? 0 : 1;

  NSString *local = adv[CBAdvertisementDataLocalNameKey];
  if (local.length == 0) local = peripheral.name;
  if (local.length > 0) {
    advert.name = [local UTF8String];
    _names[idText(peripheral)] = advert.name;
  }
  advert.rssi = RSSI.intValue == 127 ? 0 : RSSI.intValue;  // 127: not available

  NSDictionary<CBUUID *, NSData *> *serviceData = adv[CBAdvertisementDataServiceDataKey];
  for (CBUUID *key in serviceData) {
    advert.serviceData.emplace_back(uuidText(key), toStd(serviceData[key]));
  }
  for (NSString *k in @[ CBAdvertisementDataServiceUUIDsKey, CBAdvertisementDataOverflowServiceUUIDsKey ]) {
    for (CBUUID *u in (NSArray<CBUUID *> *)adv[k]) advert.serviceUuids.push_back(uuidText(u));
  }

  // A connect() may be waiting for exactly this address.
  for (size_t i = 0; i < _resolving.size();) {
    std::shared_ptr<ConnectOp> op = _resolving[i];
    if (op->address == advert.address) {
      _resolving.erase(_resolving.begin() + i);
      op->resolving = false;
      op->idStr = idText(peripheral);
      [self updateScan];
      [self beginConnect:op peripheral:peripheral];
    } else {
      ++i;
    }
  }

  if (_scanFlag.load()) [self emitAdvert:advert];
}

// --------------------------------------------------------------- connect ----

- (bool)connect:(const std::string &)address
        timeoutMs:(uint32_t)timeoutMs
             link:(BleLink *)linkOut
         services:(std::vector<BleServiceInfo> *)servicesOut
        lastError:(int *)lastError {
  if (lastError) *lastError = 0;
  if ([self onQueue]) {
    // Cannot wait for CoreBluetooth callbacks from the queue that delivers them.
    NSLog(@"BBW BLE: connect() called on the CoreBluetooth queue; refused");
    return false;
  }
  if (_state.load() != BBW_BT_POWERED_ON) {
    if (lastError) *lastError = 0x23e;
    return false;
  }
  std::shared_ptr<ConnectOp> op = std::make_shared<ConnectOp>();
  op->address = address;
  dispatch_async(_q, ^{ [self startConnect:op]; });

  std::unique_lock<std::mutex> lock(op->m);
  op->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return op->done; });
  if (!op->done) {
    op->abandoned = true;  // under op->m, so finishOp cannot register a link after this
    lock.unlock();
    dispatch_async(_q, ^{ [self cancelOp:op]; });
    return false;  // lastError stays 0: our own timeout
  }
  if (!op->ok) {
    if (lastError) *lastError = op->err;
    return false;
  }
  *linkOut = op->link;
  *servicesOut = op->services;
  return true;
}

- (void)startConnect:(std::shared_ptr<ConnectOp>)op {
  if (_state.load() != BBW_BT_POWERED_ON) {
    [self failOp:op code:0x23e cancel:NO];
    return;
  }
  const std::string idStr = _book.idFor(op->address);
  CBPeripheral *p = [self peripheralForId:idStr];
  if (!p) {
    // Not heard in this run and not retrievable: scan until an advertisement maps to it.
    op->resolving = true;
    _resolving.push_back(op);
    [self updateScan];
    return;
  }
  op->idStr = idStr;
  [self beginConnect:op peripheral:p];
}

- (void)beginConnect:(std::shared_ptr<ConnectOp>)op peripheral:(CBPeripheral *)p {
  op->peripheral = p;
  p.delegate = self;
  _connecting[op->idStr] = op;
  if (p == _standing) _standingConnected = false;  // the firmware took it over
  switch (p.state) {
    case CBPeripheralStateConnected: [self startDiscovery:op]; break;
    case CBPeripheralStateConnecting: break;  // a standing connect: didConnect follows
    default: [_central connectPeripheral:p options:nil]; break;
  }
}

- (void)startDiscovery:(std::shared_ptr<ConnectOp>)op {
  [op->peripheral discoverServices:nil];
}

// Removes `op` and settles its waiting caller with a failure (unless it already gave up).
- (void)failOp:(std::shared_ptr<ConnectOp>)op code:(int)code cancel:(BOOL)cancel {
  _connecting.erase(op->idStr);
  _resolving.erase(std::remove(_resolving.begin(), _resolving.end(), op), _resolving.end());
  op->resolving = false;
  [self updateScan];
  CBPeripheral *p = op->peripheral;
  if (cancel && p && p.state != CBPeripheralStateDisconnected) [_central cancelPeripheralConnection:p];
  {
    std::lock_guard<std::mutex> lock(op->m);
    if (!op->done) {
      op->done = true;
      op->ok = false;
      op->err = code;
    }
  }
  op->cv.notify_all();
}

// The caller timed out.
- (void)cancelOp:(std::shared_ptr<ConnectOp>)op {
  auto it = _connecting.find(op->idStr);
  const bool wasConnecting = it != _connecting.end() && it->second == op;
  const bool wasResolving =
      std::find(_resolving.begin(), _resolving.end(), op) != _resolving.end();
  if (wasConnecting || wasResolving) [self failOp:op code:0 cancel:YES];
}

- (void)finishOp:(std::shared_ptr<ConnectOp>)op {
  CBPeripheral *p = op->peripheral;
  std::vector<BleServiceInfo> table;
  bool isScale = false;
  for (CBService *s in p.services) {
    BleServiceInfo si;
    si.uuid = uuidText(s.UUID);
    si.handle = (uintptr_t)(__bridge void *)s;
    for (CBCharacteristic *c in s.characteristics) {
      BleCharInfo ci;
      ci.uuid = uuidText(c.UUID);
      ci.notify = (c.properties & CBCharacteristicPropertyNotify) != 0;
      ci.indicate = (c.properties & CBCharacteristicPropertyIndicate) != 0;
      ci.write = (c.properties & CBCharacteristicPropertyWrite) != 0;
      ci.writeNoResponse = (c.properties & CBCharacteristicPropertyWriteWithoutResponse) != 0;
      ci.handle = (uintptr_t)(__bridge void *)c;
      if (bbwcb::isScaleCharacteristic(ci.uuid)) isScale = true;
      si.characteristics.push_back(ci);
    }
    table.push_back(si);
  }
  _connecting.erase(op->idStr);

  BBWLinkRec *rec = nil;
  {
    std::lock_guard<std::mutex> lock(op->m);
    if (!op->abandoned && !op->done) {
      rec = [[BBWLinkRec alloc] init];
      rec.linkId = ++_nextLink;
      rec.peripheral = p;
      _links[@(rec.linkId)] = rec;
      _linkByPeripheral[p.identifier] = rec;
      {
        std::lock_guard<std::mutex> lock2(_mu);  // order: op->m, then _mu; nothing takes them the other way
        _liveLinks.insert(rec.linkId);
      }
      op->link = rec.linkId;
      op->services = table;
      op->ok = true;
      op->done = true;
    }
  }
  if (!rec) {
    // The caller gave up meanwhile: no link, no onDisconnect.
    if (p.state != CBPeripheralStateDisconnected) [_central cancelPeripheralConnection:p];
    return;
  }
  op->cv.notify_all();
  if (isScale && _book.pairedScaleId != op->idStr) {
    // "The paired scale": the peripheral of the most recent successful firmware
    // connect whose characteristics include a scale's.
    _book.pairedScaleId = op->idStr;
    auto n = _names.find(op->idStr);
    _book.pairedScaleName = n != _names.end() ? n->second : std::string();
    _book.save();
  } else if (isScale) {
    auto n = _names.find(op->idStr);
    if (n != _names.end() && n->second != _book.pairedScaleName) {
      _book.pairedScaleName = n->second;
      _book.save();
    }
  }
}

- (void)centralManager:(CBCentralManager *)central didConnectPeripheral:(CBPeripheral *)peripheral {
  const std::string idStr = idText(peripheral);
  peripheral.delegate = self;
  auto it = _connecting.find(idStr);
  if (it != _connecting.end()) {
    [self startDiscovery:it->second];
  } else if (peripheral == _standing || idStr == _book.pairedScaleId) {
    _standing = peripheral;
    [self standingConnected:peripheral];
  } else {
    [central cancelPeripheralConnection:peripheral];  // nobody asked for this one
  }
}

- (void)centralManager:(CBCentralManager *)central
    didFailToConnectPeripheral:(CBPeripheral *)peripheral
                         error:(NSError *)error {
  auto it = _connecting.find(idText(peripheral));
  if (it == _connecting.end()) return;
  const bool cb = [error.domain isEqualToString:CBErrorDomain];
  [self failOp:it->second code:bbwcb::mapCbError(cb, error.code) cancel:NO];
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
  auto it = _connecting.find(idText(peripheral));
  if (it == _connecting.end()) return;
  std::shared_ptr<ConnectOp> op = it->second;
  if (error) {
    [self failOp:op code:0x23e cancel:YES];
    return;
  }
  op->pendingChars = static_cast<int>(peripheral.services.count);
  if (op->pendingChars == 0) {
    [self finishOp:op];
    return;
  }
  for (CBService *s in peripheral.services) [peripheral discoverCharacteristics:nil forService:s];
}

- (void)peripheral:(CBPeripheral *)peripheral
    didDiscoverCharacteristicsForService:(CBService *)service
                                   error:(NSError *)error {
  auto it = _connecting.find(idText(peripheral));
  if (it == _connecting.end()) return;
  std::shared_ptr<ConnectOp> op = it->second;
  if (error) {
    [self failOp:op code:0x23e cancel:YES];
    return;
  }
  if (--op->pendingChars <= 0) [self finishOp:op];
}

// ------------------------------------------------------------ disconnect ----

// Ends a firmware link exactly once: whoever removes it from _links reports it.
- (void)endLink:(BBWLinkRec *)rec {
  if (!_links[@(rec.linkId)]) return;
  [_links removeObjectForKey:@(rec.linkId)];
  [_linkByPeripheral removeObjectForKey:rec.peripheral.identifier];
  {
    std::lock_guard<std::mutex> lock(_mu);
    _liveLinks.erase(rec.linkId);
  }
  // Fail everything that waits on this link.
  const BleLink link = rec.linkId;
  for (auto *waiters : {&_subWaiters, &_writeWaiters}) {
    for (auto it = waiters->begin(); it != waiters->end();) {
      if (it->first.first == link) {
        for (auto &w : it->second) complete(w, false);
        it = waiters->erase(it);
      } else {
        ++it;
      }
    }
  }
  auto pw = _pendingWrites.find(link);
  if (pw != _pendingWrites.end()) {
    for (auto &p : pw->second) complete(p.waiter, false);
    _pendingWrites.erase(pw);
  }
  [self emitDisconnect:link];
}

- (void)centralManager:(CBCentralManager *)central
    didDisconnectPeripheral:(CBPeripheral *)peripheral
                      error:(NSError *)error {
  const std::string idStr = idText(peripheral);
  auto op = _connecting.find(idStr);
  if (op != _connecting.end()) {
    const bool cb = [error.domain isEqualToString:CBErrorDomain];
    [self failOp:op->second code:error ? bbwcb::mapCbError(cb, error.code) : 0x23e cancel:NO];
  }
  BBWLinkRec *rec = _linkByPeripheral[peripheral.identifier];
  if (rec) [self endLink:rec];
  if (peripheral == _standing) {
    _standingConnected = false;
    [self armStandingAfter:kStandingRearmS];
  }
}

- (void)disconnect:(BleLink)link {
  [self post:^{
    BBWLinkRec *rec = self->_links[@(link)];
    if (!rec) return;
    if (rec.peripheral.state == CBPeripheralStateDisconnected) {
      [self endLink:rec];  // already gone; nothing more will come from CoreBluetooth
    } else {
      // didDisconnectPeripheral follows and reports the one onDisconnect.
      [self->_central cancelPeripheralConnection:rec.peripheral];
    }
  }];
}

- (bool)isConnected:(BleLink)link {
  std::lock_guard<std::mutex> lock(_mu);
  return _liveLinks.count(link) > 0;
}

// ----------------------------------------------------- subscribe / write ----

// The CBCharacteristic a BleCharRef names. Queue only; nil if unknown.
- (CBCharacteristic *)characteristic:(const BleCharRef &)ref onLink:(BBWLinkRec *)rec {
  for (CBService *s in rec.peripheral.services) {
    for (CBCharacteristic *c in s.characteristics) {
      if (ref.handle != 0 && (uintptr_t)(__bridge void *)c == ref.handle) return c;
    }
  }
  if (ref.handle == 0) {
    for (CBService *s in rec.peripheral.services) {
      if (uuidText(s.UUID) != ref.serviceUuid) continue;
      for (CBCharacteristic *c in s.characteristics) {
        if (uuidText(c.UUID) == ref.uuid) return c;
      }
    }
  }
  return nil;
}

static void dropWaiter(std::map<CharKey, std::deque<std::shared_ptr<Waiter>>> &map, const CharKey &key,
                       const std::shared_ptr<Waiter> &w) {
  auto it = map.find(key);
  if (it == map.end()) return;
  it->second.erase(std::remove(it->second.begin(), it->second.end(), w), it->second.end());
  if (it->second.empty()) map.erase(it);
}

// Waits for `w`; false on timeout.
static bool waitFor(const std::shared_ptr<Waiter> &w, uint32_t ms) {
  std::unique_lock<std::mutex> lock(w->m);
  w->cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return w->done; });
  return w->done && w->ok;
}

- (bool)subscribe:(BleLink)link characteristic:(const BleCharRef &)ref {
  if ([self onQueue]) return false;  // cannot wait for its own callback
  std::shared_ptr<Waiter> w = std::make_shared<Waiter>();
  const BleCharRef r = ref;
  dispatch_async(_q, ^{
    BBWLinkRec *rec = self->_links[@(link)];
    CBCharacteristic *c = rec ? [self characteristic:r onLink:rec] : nil;
    if (!c) {
      complete(w, false);
      return;
    }
    self->_subWaiters[CharKey(link, (uintptr_t)(__bridge void *)c)].push_back(w);
    [rec.peripheral setNotifyValue:YES forCharacteristic:c];
  });
  const bool ok = waitFor(w, kSubscribeTimeoutMs);
  if (!ok) {
    const uintptr_t handle = ref.handle;
    dispatch_async(_q, ^{ dropWaiter(self->_subWaiters, CharKey(link, handle), w); });
  }
  return ok;
}

- (void)peripheral:(CBPeripheral *)peripheral
    didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)characteristic
                                          error:(NSError *)error {
  BBWLinkRec *rec = _linkByPeripheral[peripheral.identifier];
  if (!rec) return;
  const CharKey key(rec.linkId, (uintptr_t)(__bridge void *)characteristic);
  auto it = _subWaiters.find(key);
  if (it == _subWaiters.end() || it->second.empty()) return;
  std::shared_ptr<Waiter> w = it->second.front();
  it->second.pop_front();
  if (it->second.empty()) _subWaiters.erase(it);
  complete(w, error == nil);
}

- (bool)write:(BleLink)link
    characteristic:(const BleCharRef &)ref
              data:(const uint8_t *)data
            length:(size_t)length
          response:(bool)response {
  NSData *payload = [NSData dataWithBytes:data length:length];
  const BleCharRef r = ref;
  if ([self onQueue]) {
    // Inline: only a write without response can be done without waiting.
    if (response) return false;
    BBWLinkRec *rec = _links[@(link)];
    CBCharacteristic *c = rec ? [self characteristic:r onLink:rec] : nil;
    if (!c) return false;
    [rec.peripheral writeValue:payload forCharacteristic:c type:CBCharacteristicWriteWithoutResponse];
    return true;
  }
  std::shared_ptr<Waiter> w = std::make_shared<Waiter>();
  if (response) {
    dispatch_async(_q, ^{
      BBWLinkRec *rec = self->_links[@(link)];
      CBCharacteristic *c = rec ? [self characteristic:r onLink:rec] : nil;
      if (!c) {
        complete(w, false);
        return;
      }
      self->_writeWaiters[CharKey(link, (uintptr_t)(__bridge void *)c)].push_back(w);
      [rec.peripheral writeValue:payload forCharacteristic:c type:CBCharacteristicWriteWithResponse];
    });
    const bool ok = waitFor(w, kWriteTimeoutMs);
    if (!ok) {
      const uintptr_t handle = ref.handle;
      dispatch_async(_q, ^{ dropWaiter(self->_writeWaiters, CharKey(link, handle), w); });
    }
    return ok;
  }

  // Without response: wait at most ~100 ms for buffer space, then write anyway.
  dispatch_async(_q, ^{
    BBWLinkRec *rec = self->_links[@(link)];
    CBCharacteristic *c = rec ? [self characteristic:r onLink:rec] : nil;
    if (!c) {
      complete(w, false);
      return;
    }
    auto pending = self->_pendingWrites.find(link);
    const bool queued = pending != self->_pendingWrites.end() && !pending->second.empty();
    if (!queued && rec.peripheral.canSendWriteWithoutResponse) {
      [rec.peripheral writeValue:payload forCharacteristic:c type:CBCharacteristicWriteWithoutResponse];
      complete(w, true);
      return;
    }
    self->_pendingWrites[link].push_back(PendingWrite{++self->_writeSeq, c, payload, w});
  });
  {
    std::unique_lock<std::mutex> lock(w->m);
    w->cv.wait_for(lock, std::chrono::milliseconds(kWriteNoResponseWaitMs), [&] { return w->done; });
    if (w->done) return w->ok;
  }
  // Still waiting for buffer space: write it now (CoreBluetooth buffers or drops it).
  // This block runs after the one above (serial queue), so the item is queued by then
  // unless peripheralIsReadyToSendWriteWithoutResponse or a disconnect settled it.
  dispatch_async(_q, ^{
    auto pending = self->_pendingWrites.find(link);
    if (pending == self->_pendingWrites.end()) return;
    for (auto it = pending->second.begin(); it != pending->second.end(); ++it) {
      if (it->waiter != w) continue;
      BBWLinkRec *rec = self->_links[@(link)];
      if (rec) {
        [rec.peripheral writeValue:it->data forCharacteristic:it->characteristic
                              type:CBCharacteristicWriteWithoutResponse];
      }
      const bool wrote = rec != nil;
      pending->second.erase(it);
      complete(w, wrote);
      break;
    }
    if (pending->second.empty()) self->_pendingWrites.erase(pending);
  });
  return waitFor(w, 1000);
}

- (void)peripheralIsReadyToSendWriteWithoutResponse:(CBPeripheral *)peripheral {
  BBWLinkRec *rec = _linkByPeripheral[peripheral.identifier];
  if (!rec) return;
  auto it = _pendingWrites.find(rec.linkId);
  if (it == _pendingWrites.end()) return;
  while (!it->second.empty() && peripheral.canSendWriteWithoutResponse) {
    PendingWrite pw = it->second.front();
    it->second.pop_front();
    [peripheral writeValue:pw.data forCharacteristic:pw.characteristic
                      type:CBCharacteristicWriteWithoutResponse];
    complete(pw.waiter, true);
  }
  if (it->second.empty()) _pendingWrites.erase(it);
}

- (void)peripheral:(CBPeripheral *)peripheral
    didWriteValueForCharacteristic:(CBCharacteristic *)characteristic
                             error:(NSError *)error {
  BBWLinkRec *rec = _linkByPeripheral[peripheral.identifier];
  if (!rec) return;
  const CharKey key(rec.linkId, (uintptr_t)(__bridge void *)characteristic);
  auto it = _writeWaiters.find(key);
  if (it == _writeWaiters.end() || it->second.empty()) return;
  std::shared_ptr<Waiter> w = it->second.front();
  it->second.pop_front();
  if (it->second.empty()) _writeWaiters.erase(it);
  complete(w, error == nil);
}

// Notifications go straight to the firmware, from the CoreBluetooth queue.
- (void)peripheral:(CBPeripheral *)peripheral
    didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
                              error:(NSError *)error {
  if (error || !characteristic.value) return;
  BBWLinkRec *rec = _linkByPeripheral[peripheral.identifier];
  if (!rec) return;
  std::function<void(BleLink, const std::string &, const uint8_t *, size_t)> f;
  {
    std::lock_guard<std::mutex> lock(_mu);
    f = _callbacks.onNotify;
  }
  if (!f) return;
  NSData *value = characteristic.value;
  f(rec.linkId, uuidText(characteristic.UUID), static_cast<const uint8_t *>(value.bytes), value.length);
}

// ------------------------------------------- standing connect to the scale ----
//
// iOS only returns advertisements to a backgrounded app when the scan names a
// service, and the firmware scans with no filter. So the paired scale (see
// BLE.md) gets a standing connectPeripheral: iOS keeps it pending, and wakes or
// resumes the app when the scale powers on and the connection is made. We then
// give the firmware a synthetic advertisement for it; its normal connect() finds
// the peripheral already connected and goes straight to discovery. If the firmware
// does not take it within kStandingGraceS, it is cancelled (so a scale that is
// switched on but not wanted is not held awake) and retried after kStandingHoldoffS.

- (void)armStanding {
  [self armStandingAfter:0];
}

- (void)armStandingAfter:(double)seconds {
  if (seconds > 0) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)), _q, ^{
      [self armStandingAfter:0];
    });
    return;
  }
  if (!_central || _state.load() != BBW_BT_POWERED_ON || _book.pairedScaleId.empty()) return;
  if (Clock::now() < _standingHoldoff) return;
  if (_connecting.count(_book.pairedScaleId)) return;
  CBPeripheral *p = [self peripheralForId:_book.pairedScaleId];
  if (!p || _linkByPeripheral[p.identifier]) return;
  _standing = p;
  p.delegate = self;
  if (p.state == CBPeripheralStateDisconnected) {
    [_central connectPeripheral:p options:nil];  // no timeout: stays pending until the scale is on
  } else if (p.state == CBPeripheralStateConnected && !_standingConnected) {
    [self standingConnected:p];
  }
}

- (void)standingConnected:(CBPeripheral *)p {
  _standingConnected = true;
  _standingSince = Clock::now();
  [self deliverStandingAdvert];
  if (!_standingTicking) {
    _standingTicking = true;
    [self standingTick];
  }
}

- (void)deliverStandingAdvert {
  if (!_standingConnected || !_standing || !_scanFlag.load()) return;
  BleAdvert advert;
  const std::string idStr = idText(_standing);
  advert.address = [self addressForPeripheral:_standing mac:std::string()];
  advert.addressType = 1;
  auto n = _names.find(idStr);
  advert.name = n != _names.end() ? n->second : _book.pairedScaleName;
  advert.rssi = 0;
  [self emitAdvert:advert];
}

// Once a second while the standing connection waits for the firmware.
- (void)standingTick {
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), _q, ^{
    if (!self->_standingConnected) {
      self->_standingTicking = false;
      return;
    }
    const double waited =
        std::chrono::duration<double>(Clock::now() - self->_standingSince).count();
    if (waited >= kStandingGraceS) {
      self->_standingTicking = false;
      self->_standingConnected = false;
      self->_standingHoldoff = Clock::now() + std::chrono::seconds((int)kStandingHoldoffS);
      if (self->_standing && self->_standing.state != CBPeripheralStateDisconnected) {
        [self->_central cancelPeripheralConnection:self->_standing];
      }
      [self armStandingAfter:kStandingHoldoffS + 1];
      return;
    }
    [self deliverStandingAdvert];
    [self standingTick];
  });
}

@end

// ================================================ 3. adapter and C entry =====
namespace {

class CoreBluetoothBackend : public BleBackend {
 public:
  CoreBluetoothBackend(const char *dataDir, const char *restoreId)
      : impl_([[BBWCentral alloc] initWithDataDir:dataDir restoreId:restoreId]) {}

  void setCallbacks(const BleCallbacks &callbacks) override { [impl_ setCallbacks:callbacks]; }
  void init(const std::string &) override { [impl_ start]; }
  void setActiveScan(bool) override {}  // CoreBluetooth always reports scan responses
  bool scanStart() override { return [impl_ scanStart]; }
  bool scanStop() override { return [impl_ scanStop]; }
  bool isScanning() override { return [impl_ isScanning]; }
  bool connect(const std::string &address, uint8_t, uint32_t timeoutMs, BleLink *link,
               std::vector<BleServiceInfo> *services, int *lastError) override {
    return [impl_ connect:address timeoutMs:timeoutMs link:link services:services lastError:lastError];
  }
  void disconnect(BleLink link) override { [impl_ disconnect:link]; }
  bool isConnected(BleLink link) override { return [impl_ isConnected:link]; }
  bool subscribe(BleLink link, const BleCharRef &characteristic, bool) override {
    return [impl_ subscribe:link characteristic:characteristic];
  }
  bool write(BleLink link, const BleCharRef &characteristic, const uint8_t *data, size_t length,
             bool response) override {
    return [impl_ write:link characteristic:characteristic data:data length:length response:response];
  }
  int state() { return [impl_ bluetoothState]; }

 private:
  BBWCentral *impl_;
};

std::mutex g_createMutex;
CoreBluetoothBackend *g_backend = nullptr;

}  // namespace

extern "C" void *bbw_corebluetooth_backend_create(const char *dataDir, const char *restoreId) {
  std::lock_guard<std::mutex> lock(g_createMutex);
  if (!g_backend) g_backend = new CoreBluetoothBackend(dataDir, restoreId);
  return static_cast<BleBackend *>(g_backend);
}

extern "C" int bbw_corebluetooth_state(void) {
  CoreBluetoothBackend *backend;
  {
    std::lock_guard<std::mutex> lock(g_createMutex);
    backend = g_backend;
  }
  const int known = backend ? backend->state() : BBW_BT_UNKNOWN;
  if (known != BBW_BT_UNKNOWN) return known;
  // No central yet (or no answer yet): the permission tells most of it.
  if (@available(iOS 13.1, *)) {
    switch ([CBManager authorization]) {
      case CBManagerAuthorizationDenied:
      case CBManagerAuthorizationRestricted: return BBW_BT_UNAUTHORIZED;
      default: break;
    }
  }
  return BBW_BT_UNKNOWN;
}

#endif  // BBW_CB_HELPERS_ONLY
