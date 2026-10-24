// The emulator's side of two things the board does with hardware and TLS:
// the device key (src/platform/DeviceKey.h) and the connection to the shared
// library (src/platform/LibraryLink.h).
//
// The key. If this PC has OpenSSL (Git for Windows brings one), the emulator
// has a real ECDSA P-256 key of its own, kept in its data folder, and signs
// with it by running openssl: its records are then as checkable as a board's.
// Without OpenSSL it falls back to a stand-in whose algorithm reads
// "EMULATOR-UNSIGNED", whose "signatures" nobody can check, and which the
// library refuses.
//
// The library. With a repository set on the Setup page, a real key, and the
// GitHub CLI installed and logged in on this PC, records go to that
// repository's inbox through `gh`, and status/ and index/ are read from it.
// No token is typed into or stored by the emulator: the CLI's own login is
// used. Otherwise the "library" is a folder beside the emulator's data,
// published/, laid out as the repository is.
//
// One thing the emulator never sends to a real library: measurements. Its
// shots come from a simulated machine. Dial-ins and shots are written to the
// folder only; what it sends for real are descriptions its owner typed,
// coffees and grinders, which go to review like anyone's.

#include <Preferences.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "emu.h"
#include "platform/DeviceKey.h"
#include "platform/LibraryLink.h"

namespace {

namespace fs = std::filesystem;

std::string dataPath(const std::string &relative) {
  return (fs::path(emu::g_options.dataDir) / relative).string();
}

// Runs a command line, returns its exit code; what it printed goes to `out`.
int run(const std::string &command, std::string *out = nullptr) {
  // cmd.exe strips one pair of outer quotes: give it a pair to strip.
  const std::string wrapped = "\"" + command + " 2>&1\"";
  FILE *pipe = _popen(wrapped.c_str(), "rb");
  if (!pipe) return -1;
  char chunk[512];
  size_t n;
  std::string text;
  while ((n = fread(chunk, 1, sizeof(chunk), pipe)) > 0) text.append(chunk, n);
  if (out) *out = text;
  return _pclose(pipe);
}

std::vector<uint8_t> readFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool writeFile(const std::string &path, const void *data, size_t size) {
  std::error_code error;
  fs::create_directories(fs::path(path).parent_path(), error);
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(data, 1, size, f) == size;
  fclose(f);
  return ok;
}

// "openssl" if it is on the PATH, else where Git for Windows keeps it.
std::string findOpenssl() {
  const char *candidates[] = {
      "openssl",
      "C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe",
      "C:\\Program Files\\Git\\usr\\bin\\openssl.exe",
      "C:\\Program Files (x86)\\Git\\mingw64\\bin\\openssl.exe",
  };
  for (const char *candidate : candidates) {
    const std::string quoted = std::string("\"") + candidate + "\"";
    std::string said;
    if (run(quoted + " version", &said) == 0 && said.find("OpenSSL") != std::string::npos) {
      return quoted;
    }
  }
  return "";
}

// ------------------------------------------------------------------ signers --

// A real key, used through the openssl program.
class OpensslSigner : public bbw::ISigner {
 public:
  bool begin(const std::string &openssl) {
    openssl_ = openssl;
    key_ = dataPath("nvs/bbw/devkey_emu.pem");
    std::error_code error;
    fs::create_directories(fs::path(key_).parent_path(), error);
    if (!fs::exists(key_, error) &&
        run(openssl_ + " ecparam -name prime256v1 -genkey -noout -out \"" + key_ + "\"") != 0) {
      return false;
    }
    const std::string pub = dataPath("nvs/bbw/devkey_emu.pub");
    if (run(openssl_ + " ec -in \"" + key_ + "\" -pubout -outform DER -out \"" + pub + "\"") != 0) {
      return false;
    }
    // The DER public key ends with the point itself: 04 | X | Y.
    const std::vector<uint8_t> der = readFile(pub);
    if (der.size() < bbw::PUBLIC_KEY_LEN || der[der.size() - bbw::PUBLIC_KEY_LEN] != 0x04) return false;
    memcpy(publicKey_, der.data() + der.size() - bbw::PUBLIC_KEY_LEN, bbw::PUBLIC_KEY_LEN);
    uint8_t probe[32] = {1}, signature[bbw::SIGNATURE_LEN];
    ready_ = true;
    ready_ = sign(probe, signature);  // it must actually work before it is relied on
    return ready_;
  }
  bool ready() const override { return ready_; }
  const uint8_t *publicKey() const override { return publicKey_; }
  const char *algorithm() const override { return "ES256"; }
  bool sign(const uint8_t hash[32], uint8_t signature[bbw::SIGNATURE_LEN]) override {
    if (!ready_) return false;
    const std::string in = dataPath("nvs/bbw/sign_in.bin"), out = dataPath("nvs/bbw/sign_out.der");
    if (!writeFile(in, hash, 32)) return false;
    // pkeyutl signs the digest as given: it does not hash it again.
    if (run(openssl_ + " pkeyutl -sign -inkey \"" + key_ + "\" -in \"" + in + "\" -out \"" + out + "\"") != 0) {
      return false;
    }
    const std::vector<uint8_t> der = readFile(out);
    std::error_code error;
    fs::remove(in, error);
    fs::remove(out, error);
    // SEQUENCE { INTEGER r, INTEGER s }, each to be right-aligned in 32 bytes.
    size_t at = 0;
    if (der.size() < 8 || der[at++] != 0x30) return false;
    if (der[at] & 0x80) at += 1 + (der[at] & 0x7f); else at += 1;
    memset(signature, 0, bbw::SIGNATURE_LEN);
    for (int part = 0; part < 2; ++part) {
      if (at + 2 > der.size() || der[at] != 0x02) return false;
      size_t length = der[at + 1];
      at += 2;
      if (at + length > der.size()) return false;
      const uint8_t *value = der.data() + at;
      at += length;
      while (length > 32 && *value == 0) {  // a leading zero keeps the number positive
        ++value;
        --length;
      }
      if (length > 32) return false;
      memcpy(signature + 32 * part + (32 - length), value, length);
    }
    return true;
  }

 private:
  std::string openssl_, key_;
  uint8_t publicKey_[bbw::PUBLIC_KEY_LEN] = {};
  bool ready_ = false;
};

// No OpenSSL: the same shape, and plainly not a signature.
class StandInSigner : public bbw::ISigner {
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
  bool sign(const uint8_t hash[32], uint8_t signature[bbw::SIGNATURE_LEN]) override {
    bbw::Sha256 a, b;
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
    bbw::Sha256 h;
    h.update(secret_, sizeof(secret_));
    h.update(tag, 1);
    h.finish(out);
  }
  uint8_t secret_[32] = {};
  uint8_t publicKey_[bbw::PUBLIC_KEY_LEN] = {};
  bool ready_ = false;
};

OpensslSigner g_real;
StandInSigner g_standIn;
bool g_realKey = false;

// ------------------------------------------------------------- the library --

std::string g_repo;
std::string g_relay;      // the relay Setup names ("https://host"); empty: none
bool g_viaRelay = false;
bool g_keyMissing = false;
bool g_cliChecked = false, g_cliOk = false;
bbw::LibraryLinkStatus g_status;

bool cliReady() {
  if (!g_cliChecked) {
    g_cliChecked = true;
    g_cliOk = run("gh auth status") == 0;
    emu::log("emu", g_cliOk ? "library: the GitHub CLI is logged in on this PC"
                            : "library: no GitHub CLI login on this PC, records stay in the folder");
  }
  return g_cliOk;
}

// The real library, as against the folder.
bool online() { return g_viaRelay || (g_realKey && !g_repo.empty() && cliReady()); }

// Through the relay, as the board does: curl posts {"path","record"} or reads a file.
int relayPut(const char *path, const char *text) {
  static char body[bbw::SHARE_ENVELOPE_MAX * 2 + 256];
  bbw::JsonOut j(body, sizeof(body));
  j.open();
  j.text("path", path);
  j.text("record", text);
  j.close();
  if (!j.ok()) return -2;
  const std::string file = dataPath("nvs/bbw/relay_put.json");
  if (!writeFile(file, body, j.length())) return -2;
  std::string said;
  run("curl -s -m 20 -o NUL -w \"%{http_code}\" -H \"Content-Type: application/json\" --data-binary \"@" + file +
          "\" \"" + g_relay + "/library/put\"",
      &said);
  std::error_code error;
  fs::remove(file, error);
  const int code = atoi(said.c_str());
  return code == 0 ? -1 : code;
}

bool folderPut(const char *path, const char *text) {
  return writeFile(dataPath(std::string("published/") + path), text, strlen(text));
}

bool folderGet(const char *path, char *out, size_t size) {
  const std::vector<uint8_t> bytes = readFile(dataPath(std::string("published/") + path));
  if (bytes.empty() || bytes.size() >= size) return false;
  memcpy(out, bytes.data(), bytes.size());
  out[bytes.size()] = '\0';
  return true;
}

void note(int code, const char *problem) {
  g_status.lastHttp = code;
  strncpy(g_status.problem, problem, sizeof(g_status.problem) - 1);
  g_status.problem[sizeof(g_status.problem) - 1] = '\0';
  if (!problem[0]) g_status.lastOkEpoch = static_cast<uint32_t>(time(nullptr));
}

}  // namespace

namespace bbw {

bool beginDeviceKey() {
  const std::string openssl = findOpenssl();
  g_realKey = !openssl.empty() && g_real.begin(openssl);
  emu::log("emu", g_realKey ? "key: a real ES256 key, signing through openssl"
                            : "key: no OpenSSL on this PC, using the unsigned stand-in");
  return g_realKey ? true : g_standIn.begin();
}

ISigner &deviceSigner() { return g_realKey ? static_cast<ISigner &>(g_real) : g_standIn; }

void libraryLinkSetRelay(const char *base) { g_relay = base ? base : ""; }

bool libraryTakeKeyMissing() {
  const bool missing = g_keyMissing;
  g_keyMissing = false;
  return missing;
}

void libraryLinkConfigure(const char *repo, const char *) {
  g_repo = repo ? repo : "";
  // The relay only when Setup names one: an emulator's records go to a test library, not the real one.
  g_viaRelay = !g_relay.empty() && g_realKey;
  g_keyMissing = false;
  g_cliChecked = false;
  g_status = LibraryLinkStatus{};
}

bool libraryLinkReady() { return true; }  // there is always the folder

bool libraryPut(const char *path, const char *text) {
  // Simulated shots are not measurements: they never leave this PC.
  const bool measurement = strncmp(path, "dialins/", 8) == 0 || strncmp(path, "shots/", 6) == 0;
  if (!online() || measurement) return folderPut(path, text);
  if (g_viaRelay) {
    const int code = relayPut(path, text);
    if (code == 201 || code == 200 || code == 422) {
      note(code, "");
      folderPut(path, text);  // and a copy to look at
      return true;
    }
    if (code == 409) g_keyMissing = true;
    note(code, code == 409 ? "the relay asked for this controller's key first"
             : code == 403 ? "the relay refused this controller"
             : code == 429 ? "the relay's daily limit is reached"
             : code < 0 ? "the relay could not be reached" : "the relay answered with an error");
    emu::log("emu", "library: relay answered %d for %s", code, path);
    return false;
  }
  // The same request the board makes: PUT .../contents/inbox/<path> on `inbox`.
  static char encoded[SHARE_ENVELOPE_MAX * 4 / 3 + 8];
  if (base64Encode(reinterpret_cast<const uint8_t *>(text), strlen(text), encoded, sizeof(encoded)) == 0) {
    return false;
  }
  const std::string body =
      std::string("{\"message\":\"Record from a controller (emulator)\",\"branch\":\"inbox\",\"content\":\"") +
      encoded + "\"}";
  const std::string file = dataPath("nvs/bbw/put_body.json");
  if (!writeFile(file, body.data(), body.size())) return false;
  std::string said;
  const int code = run("gh api -X PUT \"repos/" + g_repo + "/contents/inbox/" + path +
                           "\" -H \"X-GitHub-Api-Version: 2022-11-28\" --input \"" + file + "\" --silent",
                       &said);
  std::error_code error;
  fs::remove(file, error);
  // Already there: it arrived on an earlier try.
  if (code == 0 || said.find("HTTP 422") != std::string::npos) {
    note(code == 0 ? 201 : 422, "");
    folderPut(path, text);  // and a copy to look at
    return true;
  }
  note(-1, said.find("HTTP 404") != std::string::npos ? "repository or inbox branch not found"
                                                       : "GitHub did not take the record");
  emu::log("emu", "library: %s", said.substr(0, 160).c_str());
  return false;
}

void libraryLinkRelease() {}

bool libraryGet(const char *path, char *out, size_t size) {
  if (!online()) return folderGet(path, out, size);
  if (g_viaRelay) {
    std::string said;
    run("curl -s -m 20 -w \"\\n@@@%{http_code}\" \"" + g_relay + "/library/get?path=" + path + "\"", &said);
    const size_t mark = said.rfind("\n@@@");
    const int code = mark == std::string::npos ? -1 : atoi(said.c_str() + mark + 4);
    if (code == 404) {
      note(404, "");  // not there: an answer
      return false;
    }
    if (code != 200) {
      note(code, "the relay could not be read");
      return false;
    }
    said.resize(mark);
    note(200, "");
    if (said.empty() || said.size() >= size) return false;
    memcpy(out, said.data(), said.size());
    out[said.size()] = '\0';
    return true;
  }
  std::string said;
  const int code = run("gh api \"repos/" + g_repo + "/contents/" + path +
                           "?ref=main\" -H \"Accept: application/vnd.github.raw+json\"",
                       &said);
  if (code != 0) {
    if (said.find("HTTP 404") != std::string::npos) note(404, "");  // not there: an answer
    else note(-1, "GitHub could not be read");
    return false;
  }
  note(200, "");
  if (said.empty() || said.size() >= size) return false;
  memcpy(out, said.data(), said.size());
  out[said.size()] = '\0';
  return true;
}

LibraryLinkStatus libraryLinkStatus() {
  LibraryLinkStatus status = g_status;
  status.configured = !g_repo.empty() || g_viaRelay;
  status.ready = true;
  status.viaRelay = g_viaRelay;
  strncpy(status.repository, g_viaRelay ? g_relay.c_str() : g_repo.c_str(), sizeof(status.repository) - 1);
  const char *via = g_viaRelay ? "the relay Setup names (curl); simulated shots stay here"
                    : online() ? "the GitHub CLI on this PC; simulated shots stay here"
                               : "a folder beside the emulator's data (published/), not the library";
  strncpy(status.via, via, sizeof(status.via) - 1);
  status.toLibrary = online();
  return status;
}

}  // namespace bbw
