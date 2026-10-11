// Stand-in for the board's device key (src/platform/DeviceKey.h). The PC
// build has no mbedTLS, so this makes something with the same shape and
// names it for what it is: the algorithm reads "EMULATOR-UNSIGNED", the
// "public key" is not a point on any curve, and the "signature" cannot be
// checked by anyone. It exists so the rest (records, envelope, outbox,
// sending) runs here exactly as on the board. The library refuses it.
//
// Also here: where the emulator "sends" records, a folder beside its data.

#include <Preferences.h>

#include <filesystem>
#include <random>
#include <string>

#include "emu.h"
#include "platform/DeviceKey.h"

namespace bbw {

namespace {

class StandInSigner : public ISigner {
 public:
  bool begin() {
    Preferences prefs;
    prefs.begin("bbw", false);
    if (prefs.getBytesLength("devkey_emu") != sizeof(secret_) ||
        prefs.getBytes("devkey_emu", secret_, sizeof(secret_)) != sizeof(secret_)) {
      std::random_device source;
      for (uint8_t &b : secret_) b = static_cast<uint8_t>(source());
      prefs.putBytes("devkey_emu", secret_, sizeof(secret_));
    }
    publicKey_[0] = 0x04;
    mix("x", publicKey_ + 1);
    mix("y", publicKey_ + 33);
    ready_ = true;
    return true;
  }
  bool ready() const override { return ready_; }
  const uint8_t *publicKey() const override { return publicKey_; }
  const char *algorithm() const override { return "EMULATOR-UNSIGNED"; }
  bool sign(const uint8_t hash[32], uint8_t signature[SIGNATURE_LEN]) override {
    Sha256 a, b;
    a.update(secret_, sizeof(secret_));
    a.update(hash, 32);
    a.finish(signature);
    b.update(hash, 32);
    b.update(secret_, sizeof(secret_));
    b.finish(signature + 32);
    return true;
  }

 private:
  void mix(const char *tag, uint8_t out[32]) {
    Sha256 h;
    h.update(secret_, sizeof(secret_));
    h.update(tag, 1);
    h.finish(out);
  }
  uint8_t secret_[32] = {};
  uint8_t publicKey_[PUBLIC_KEY_LEN] = {};
  bool ready_ = false;
};

StandInSigner g_signer;

}  // namespace

bool beginDeviceKey() { return g_signer.begin(); }
ISigner &deviceSigner() { return g_signer; }

}  // namespace bbw

// The emulator's "repository": <data dir>/published/<path>.
bool emuPublish(const char *path, const char *text) {
  namespace fs = std::filesystem;
  std::error_code error;
  const fs::path target = fs::path(emu::g_options.dataDir) / "published" / path;
  fs::create_directories(target.parent_path(), error);
  if (error) return false;
  FILE *f = fopen(target.string().c_str(), "wb");
  if (!f) return false;
  const size_t length = strlen(text);
  const bool ok = fwrite(text, 1, length, f) == length;
  fclose(f);
  return ok;
}

// What the "library" tells this controller: published/status/<key id>.json.
// Nothing writes it here; put one there by hand to try the review states.
bool emuFetchStatus(const char *kid, char *out, size_t size) {
  namespace fs = std::filesystem;
  const fs::path source =
      fs::path(emu::g_options.dataDir) / "published" / "status" / (std::string(kid) + ".json");
  FILE *f = fopen(source.string().c_str(), "rb");
  if (!f) return false;
  const size_t length = fread(out, 1, size - 1, f);
  fclose(f);
  out[length] = '\0';
  return length > 0;
}

bool emuFetchFile(const char *path, char *out, size_t size) {
  namespace fs = std::filesystem;
  const fs::path source = fs::path(emu::g_options.dataDir) / "published" / path;
  FILE *f = fopen(source.string().c_str(), "rb");
  if (!f) return false;
  const size_t length = fread(out, 1, size - 1, f);
  fclose(f);
  out[length] = '\0';
  return length > 0;
}
