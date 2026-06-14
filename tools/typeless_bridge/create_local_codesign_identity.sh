#!/usr/bin/env bash
set -euo pipefail

IDENTITY_NAME="${BRIDGE_CODESIGN_IDENTITY:-M5StopWatch Local Code Signing}"
KEYCHAIN="${BRIDGE_CODESIGN_KEYCHAIN:-$HOME/Library/Keychains/login.keychain-db}"

if security find-identity -v -p codesigning "$KEYCHAIN" | grep -Fq "$IDENTITY_NAME"; then
  echo "Code signing identity already exists: $IDENTITY_NAME"
  exit 0
fi

TMP_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cat > "$TMP_DIR/codesign.cnf" <<EOF
[ req ]
default_bits = 2048
prompt = no
default_md = sha256
distinguished_name = dn
x509_extensions = ext

[ dn ]
CN = $IDENTITY_NAME

[ ext ]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = codeSigning
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
EOF

/usr/bin/openssl req \
  -new \
  -newkey rsa:2048 \
  -x509 \
  -days 3650 \
  -nodes \
  -config "$TMP_DIR/codesign.cnf" \
  -keyout "$TMP_DIR/codesign.key" \
  -out "$TMP_DIR/codesign.crt" >/dev/null 2>&1

P12_PASSWORD="$(uuidgen)"
/usr/bin/openssl pkcs12 \
  -export \
  -inkey "$TMP_DIR/codesign.key" \
  -in "$TMP_DIR/codesign.crt" \
  -name "$IDENTITY_NAME" \
  -out "$TMP_DIR/codesign.p12" \
  -passout "pass:$P12_PASSWORD" >/dev/null 2>&1

security import "$TMP_DIR/codesign.p12" \
  -k "$KEYCHAIN" \
  -P "$P12_PASSWORD" \
  -T /usr/bin/codesign >/dev/null

security add-trusted-cert \
  -r trustRoot \
  -p codeSign \
  -k "$KEYCHAIN" \
  "$TMP_DIR/codesign.crt" >/dev/null

security find-identity -v -p codesigning "$KEYCHAIN" | grep -F "$IDENTITY_NAME"
