#!/bin/bash
# Creates (once) a self-signed code signing identity in a dedicated keychain,
# so every rebuild of FucairEdge.app carries the same signature and macOS keeps
# its Accessibility and Input Monitoring approval. No Apple developer account
# is needed. The keychain password is random and stays in ~/.fucairserver.
# Usage: source signing.sh; then sign with: codesign --keychain "$sign_keychain" -s "$sign_name" ...
set -euo pipefail
sign_dir="$HOME/.fucairserver/signing"
sign_keychain="$sign_dir/fucair-signing.keychain-db"
sign_name="Fucair Local Code Signing"
mkdir -p "$sign_dir"
chmod 700 "$sign_dir"

if [ ! -f "$sign_dir/password" ]; then
    (umask 077; /usr/bin/openssl rand -hex 24 > "$sign_dir/password")
fi
sign_password="$(cat "$sign_dir/password")"

if [ ! -f "$sign_keychain" ]; then
    security create-keychain -p "$sign_password" "$sign_keychain"
    security set-keychain-settings "$sign_keychain"   # no auto-lock timeout
fi
security unlock-keychain -p "$sign_password" "$sign_keychain"

if ! security find-certificate -c "$sign_name" "$sign_keychain" >/dev/null 2>&1; then
    work="$(mktemp -d)"
    trap 'rm -rf "$work"' EXIT
    cat > "$work/cert.cnf" <<CNF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $sign_name
[ext]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
CNF
    /usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 7300 -config "$work/cert.cnf" \
        -keyout "$work/key.pem" -out "$work/cert.pem" 2>/dev/null
    /usr/bin/openssl pkcs12 -export -inkey "$work/key.pem" -in "$work/cert.pem" \
        -name "$sign_name" -passout pass:"$sign_password" -out "$work/identity.p12"
    security import "$work/identity.p12" -k "$sign_keychain" -P "$sign_password" -T /usr/bin/codesign >/dev/null
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$sign_password" "$sign_keychain" >/dev/null
    echo "created signing identity: $sign_name"
fi

# codesign looks identities up through the user's keychain search list.
if ! security list-keychains -d user | grep -q "fucair-signing"; then
    security list-keychains -d user -s $(security list-keychains -d user | tr -d '"') "$sign_keychain"
fi
