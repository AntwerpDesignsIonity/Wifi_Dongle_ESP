#!/usr/bin/env python3
"""
gen_certs.py – Generate a self-signed TLS certificate for ionity.today.local
────────────────────────────────────────────────────────────────────────────
Run ONCE before building the ESP-IDF firmware.  The files produced here are
embedded into the firmware binary at build time (see CMakeLists.txt).

Requires: pip install cryptography

Usage:
    cd firmware/certs
    python gen_certs.py

Output:
    server_cert.pem   (DER-encoded, X.509 v3, PEM wrapped)
    server_key.pem    (RSA-2048 private key, PEM wrapped)

────────────────────────────────────────────────────────────────────────────
IONITY (Pty) Ltd - South Africa
CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
────────────────────────────────────────────────────────────────────────────
"""

import datetime
import ipaddress
import os
import sys

try:
    from cryptography import x509
    from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509 import DNSName, IPAddress
except ImportError:
    sys.exit(
        "[ERROR] 'cryptography' package not found.\n"
        "        Install it with:  pip install cryptography"
    )

# ── Output paths ─────────────────────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CERT_FILE  = os.path.join(SCRIPT_DIR, "server_cert.pem")
KEY_FILE   = os.path.join(SCRIPT_DIR, "server_key.pem")

# ── Certificate parameters ────────────────────────────────────────────────────
HOSTNAME    = "ionity.today"
FQDN_LOCAL  = "ionity.today.local"  # mDNS FQDN
IP_USB      = "192.168.7.1"         # USB-side management IP
IP_PORTAL   = "192.168.4.1"         # Soft-AP portal IP
VALID_DAYS  = 3650                  # 10 years


def main() -> None:
    print(f"[+] Generating RSA-2048 private key …")
    key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
    )

    now = datetime.datetime.now(datetime.timezone.utc)

    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME,             "ZA"),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME,   "Western Cape"),
        x509.NameAttribute(NameOID.LOCALITY_NAME,            "Cape Town"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME,        "IONITY (Pty) Ltd"),
        x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, "WiFi Dongle"),
        x509.NameAttribute(NameOID.COMMON_NAME,              FQDN_LOCAL),
    ])

    san = x509.SubjectAlternativeName([
        DNSName(FQDN_LOCAL),
        DNSName(HOSTNAME),
        DNSName("*.ionity.today.local"),
        DNSName("localhost"),
        IPAddress(ipaddress.ip_address(IP_USB)),
        IPAddress(ipaddress.ip_address(IP_PORTAL)),
    ])

    print(f"[+] Building certificate for {FQDN_LOCAL} …")
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now)
        .not_valid_after(now + datetime.timedelta(days=VALID_DAYS))
        .add_extension(san, critical=False)
        .add_extension(
            x509.BasicConstraints(ca=True, path_length=None),
            critical=True,
        )
        .add_extension(
            x509.KeyUsage(
                digital_signature=True, key_cert_sign=True,
                content_commitment=False, key_encipherment=True,
                data_encipherment=False, key_agreement=False,
                crl_sign=False, encipher_only=False, decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(
            x509.ExtendedKeyUsage([
                ExtendedKeyUsageOID.SERVER_AUTH,
                ExtendedKeyUsageOID.CLIENT_AUTH,
            ]),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )

    # ── Write private key ──────────────────────────────────────────────────
    key_pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption(),
    )
    with open(KEY_FILE, "wb") as f:
        f.write(key_pem)
    print(f"[+] Private key written to:  {KEY_FILE}")

    # ── Write certificate ──────────────────────────────────────────────────
    cert_pem = cert.public_bytes(serialization.Encoding.PEM)
    with open(CERT_FILE, "wb") as f:
        f.write(cert_pem)
    print(f"[+] Certificate written to:  {CERT_FILE}")

    # ── Summary ───────────────────────────────────────────────────────────
    print("")
    print("┌───────────────────────────────────── Certificate summary ─────┐")
    print(f"│  Subject : {FQDN_LOCAL:<52}│")
    print(f"│  SANs    : {FQDN_LOCAL}, {IP_USB}, {IP_PORTAL:<22}│")
    print(f"│  Valid   : {VALID_DAYS} days from now                                  │")
    print("└───────────────────────────────────────────────────────────────┘")
    print("")
    print("IMPORTANT: This is a self-signed certificate.  To avoid browser")
    print("  security warnings, install server_cert.pem as a trusted CA in")
    print("  Windows Certificate Manager (certmgr.msc) or macOS Keychain.")
    print("  The IONITY Windows Companion App can do this automatically.")
    print("")


if __name__ == "__main__":
    main()
