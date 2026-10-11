// ECDSA P-256 with the mbedTLS that comes with the ESP32 core. Not built for
// the emulator (see DeviceKey.h). Not yet run on a real board.
#ifndef BBW_EMULATOR

#include "DeviceKey.h"

#include <Arduino.h>
#include <Preferences.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>

#include "Log.h"

namespace bbw {

namespace {

constexpr const char *NVS_NAMESPACE = "bbw";
constexpr const char *NVS_KEY = "devkey";
constexpr uint16_t KEY_VERSION = 1;

struct StoredKey {
  uint16_t version;
  uint8_t privateKey[32];
  uint8_t publicKey[PUBLIC_KEY_LEN];
};

class MbedSigner : public ISigner {
 public:
  bool begin() {
    mbedtls_ecp_group_init(&group_);
    mbedtls_mpi_init(&d_);
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&random_);
    static const char purpose[] = "bbw device key";
    if (mbedtls_ctr_drbg_seed(&random_, mbedtls_entropy_func, &entropy_,
                              reinterpret_cast<const unsigned char *>(purpose),
                              sizeof(purpose) - 1) != 0 ||
        mbedtls_ecp_group_load(&group_, MBEDTLS_ECP_DP_SECP256R1) != 0) {
      return false;
    }
    StoredKey stored = {};
    if (load(stored)) {
      if (mbedtls_mpi_read_binary(&d_, stored.privateKey, sizeof(stored.privateKey)) != 0) {
        return false;
      }
    } else {
      mbedtls_ecp_point q;
      mbedtls_ecp_point_init(&q);
      size_t length = 0;
      const bool made =
          mbedtls_ecp_gen_keypair(&group_, &d_, &q, mbedtls_ctr_drbg_random, &random_) == 0 &&
          mbedtls_mpi_write_binary(&d_, stored.privateKey, sizeof(stored.privateKey)) == 0 &&
          mbedtls_ecp_point_write_binary(&group_, &q, MBEDTLS_ECP_PF_UNCOMPRESSED, &length,
                                         stored.publicKey, sizeof(stored.publicKey)) == 0 &&
          length == PUBLIC_KEY_LEN;
      mbedtls_ecp_point_free(&q);
      stored.version = KEY_VERSION;
      if (!made || !save(stored)) return false;
      LOGI("key: new device key made");
    }
    memcpy(publicKey_, stored.publicKey, PUBLIC_KEY_LEN);
    memset(&stored, 0, sizeof(stored));
    ready_ = true;
    return true;
  }

  bool ready() const override { return ready_; }
  const uint8_t *publicKey() const override { return publicKey_; }
  const char *algorithm() const override { return "ES256"; }

  bool sign(const uint8_t hash[32], uint8_t signature[SIGNATURE_LEN]) override {
    if (!ready_) return false;
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    const bool ok =
        mbedtls_ecdsa_sign(&group_, &r, &s, &d_, hash, 32, mbedtls_ctr_drbg_random, &random_) == 0 &&
        mbedtls_mpi_write_binary(&r, signature, 32) == 0 &&
        mbedtls_mpi_write_binary(&s, signature + 32, 32) == 0;
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ok;
  }

 private:
  static bool load(StoredKey &out) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    const bool ok = prefs.getBytesLength(NVS_KEY) == sizeof(out) &&
                    prefs.getBytes(NVS_KEY, &out, sizeof(out)) == sizeof(out) &&
                    out.version == KEY_VERSION;
    prefs.end();
    return ok;
  }
  static bool save(const StoredKey &key) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    const bool ok = prefs.putBytes(NVS_KEY, &key, sizeof(key)) == sizeof(key);
    prefs.end();
    return ok;
  }

  mbedtls_ecp_group group_;
  mbedtls_mpi d_;
  mbedtls_entropy_context entropy_;
  mbedtls_ctr_drbg_context random_;
  uint8_t publicKey_[PUBLIC_KEY_LEN] = {};
  bool ready_ = false;
};

MbedSigner g_signer;

}  // namespace

bool beginDeviceKey() {
  const bool ok = g_signer.begin();
  if (!ok) LOGW("key: no device key, nothing will be shared");
  return ok;
}

ISigner &deviceSigner() { return g_signer; }

}  // namespace bbw

#endif  // BBW_EMULATOR
