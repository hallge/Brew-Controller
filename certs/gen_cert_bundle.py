#!/usr/bin/env python3
"""Builds x509_crt_bundle.bin, the root certificates the controller trusts when it reads a shop's
page for a subscription card (src/platform/PagePreview.cpp).

The input is Mozilla's list of root certificates as curl publishes it:
    https://curl.se/ca/cacert.pem   (check it against https://curl.se/ca/cacert.pem.sha256)

The output is ESP-IDF's certificate bundle format (esp_crt_bundle, as made by IDF's
gen_crt_bundle.py): a big-endian count, then for each certificate, sorted by subject:
    name length (2 bytes), public key length (2 bytes), subject name (DER), public key info (DER).

Needs only the Python standard library.
    python certs/gen_cert_bundle.py cacert.pem certs/x509_crt_bundle.bin
"""
import base64
import re
import struct
import sys


def read_tlv(data, at):
    """One DER element at `at`: (tag, start of its value, end of the element)."""
    tag = data[at]
    length = data[at + 1]
    at += 2
    if length & 0x80:
        n = length & 0x7F
        length = int.from_bytes(data[at:at + n], 'big')
        at += n
    return tag, at, at + length


def subject_and_key(der):
    """The subject name and SubjectPublicKeyInfo of a certificate, both as DER."""
    _, cert, _ = read_tlv(der, 0)            # Certificate SEQUENCE
    _, tbs, tbs_end = read_tlv(der, cert)    # TBSCertificate SEQUENCE
    at = tbs
    fields = []
    while at < tbs_end:
        tag, _, end = read_tlv(der, at)
        fields.append((tag, at, end))
        at = end
    if fields[0][0] == 0xA0:                 # [0] version
        fields = fields[1:]
    # serial, signature, issuer, validity, subject, subjectPublicKeyInfo
    _, s_start, s_end = fields[4]
    _, k_start, k_end = fields[5]
    return der[s_start:s_end], der[k_start:k_end]


def main(pem_path, out_path):
    text = open(pem_path, encoding='utf-8').read()
    blocks = re.findall(r'-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----', text, re.S)
    entries = []
    for block in blocks:
        der = base64.b64decode(''.join(block.split()))
        entries.append(subject_and_key(der))
    entries.sort(key=lambda e: e[0])
    if len(entries) > 200:
        sys.exit('more than 200 certificates: CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS is 200')
    out = struct.pack('>H', len(entries))
    for name, key in entries:
        out += struct.pack('>HH', len(name), len(key)) + name + key
    open(out_path, 'wb').write(out)
    print(f'{len(entries)} certificates, {len(out)} bytes -> {out_path}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
