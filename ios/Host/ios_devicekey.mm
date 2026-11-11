// The device key's P-256 primitives on Security.framework (see net_portable.h and NET.md). The key
// itself is persisted by net_portable.cpp in the Preferences stand-in, in the board's own record
// format, so a backup carries it as on the board. Nothing here touches the Keychain.
#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cstring>

#include "net_portable.h"

namespace bbw {
namespace hostnet {

bool hostP256Available() { return true; }

// blob: the X9.63 private key, 04 | X | Y | K (97 bytes).
bool hostP256Generate(uint8_t blob[KEY_BLOB_LEN]) {
  @autoreleasepool {
    NSDictionary *attributes = @{
      (__bridge id)kSecAttrKeyType : (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
      (__bridge id)kSecAttrKeySizeInBits : @256,
      (__bridge id)kSecAttrIsPermanent : @NO,  // in memory only: Preferences keeps it
    };
    CFErrorRef error = nullptr;
    SecKeyRef key = SecKeyCreateRandomKey((__bridge CFDictionaryRef)attributes, &error);
    if (!key) {
      if (error) CFRelease(error);
      return false;
    }
    CFDataRef data = SecKeyCopyExternalRepresentation(key, &error);
    CFRelease(key);
    if (!data) {
      if (error) CFRelease(error);
      return false;
    }
    const bool ok = CFDataGetLength(data) == (CFIndex)KEY_BLOB_LEN;
    if (ok) memcpy(blob, CFDataGetBytePtr(data), KEY_BLOB_LEN);
    CFRelease(data);
    return ok;
  }
}

bool hostP256Sign(const uint8_t blob[KEY_BLOB_LEN], const uint8_t hash[32], uint8_t *der, size_t *derLength) {
  @autoreleasepool {
    NSData *raw = [NSData dataWithBytes:blob length:KEY_BLOB_LEN];
    NSDictionary *attributes = @{
      (__bridge id)kSecAttrKeyType : (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
      (__bridge id)kSecAttrKeyClass : (__bridge id)kSecAttrKeyClassPrivate,
      (__bridge id)kSecAttrKeySizeInBits : @256,
    };
    CFErrorRef error = nullptr;
    SecKeyRef key = SecKeyCreateWithData((__bridge CFDataRef)raw, (__bridge CFDictionaryRef)attributes, &error);
    if (!key) {
      if (error) CFRelease(error);
      return false;
    }
    NSData *digest = [NSData dataWithBytes:hash length:32];
    // The digest is already the SHA-256 of the message: signed as it is, which is what mbedTLS did.
    CFDataRef signature = SecKeyCreateSignature(key, kSecKeyAlgorithmECDSASignatureDigestX962SHA256,
                                                (__bridge CFDataRef)digest, &error);
    CFRelease(key);
    if (!signature) {
      if (error) CFRelease(error);
      return false;
    }
    const size_t n = (size_t)CFDataGetLength(signature);
    const bool ok = n <= *derLength;
    if (ok) {
      memcpy(der, CFDataGetBytePtr(signature), n);
      *derLength = n;
    }
    CFRelease(signature);
    return ok;
  }
}

}  // namespace hostnet
}  // namespace bbw
