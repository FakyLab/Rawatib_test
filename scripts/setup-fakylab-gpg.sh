#!/usr/bin/env bash
set -euo pipefail

name_real="${FAKYLAB_GPG_NAME_REAL:-FakyLab Release Signing}"
name_email="${FAKYLAB_GPG_NAME_EMAIL:-fakylab@proton.me}"
expire_date="${FAKYLAB_GPG_EXPIRE_DATE:-2y}"
key_type="${FAKYLAB_GPG_KEY_TYPE:-ed25519}"
out_dir="${FAKYLAB_GPG_OUT_DIR:-dist/gpg}"
passphrase="${FAKYLAB_GPG_PASSPHRASE:-}"

mkdir -p "$out_dir"

if ! command -v gpg >/dev/null 2>&1; then
    echo "gpg is required but was not found in PATH." >&2
    exit 1
fi

uid="${name_real} <${name_email}>"

echo "Preparing FakyLab release signing key for:"
echo "  $uid"
echo

fingerprint="$(gpg --list-secret-keys --with-colons "$uid" 2>/dev/null | awk -F: '/^fpr:/ {print $10; exit}')"

if [[ -n "$fingerprint" ]]; then
    echo "Reusing existing secret key."
else
    echo "No existing key found. Generating a new one..."

    if [[ "$key_type" == "ed25519" ]]; then
        algo="ed25519"
    else
        algo="rsa4096"
    fi

    if [[ -n "$passphrase" ]]; then
        gpg --batch --pinentry-mode loopback --passphrase "$passphrase" \
            --quick-generate-key "$uid" "$algo" sign "$expire_date"
    else
        echo "GPG may prompt you for a passphrase in a pinentry window."
        gpg --quick-generate-key "$uid" "$algo" sign "$expire_date"
    fi

    fingerprint="$(gpg --list-secret-keys --with-colons "$uid" 2>/dev/null | awk -F: '/^fpr:/ {print $10; exit}')"
fi

if [[ -z "$fingerprint" ]]; then
    echo "Could not determine key fingerprint after generation." >&2
    echo "Try running: gpg --list-secret-keys --keyid-format LONG" >&2
    exit 1
fi

public_key_path="$out_dir/fakylab-release-public.asc"
private_key_path="$out_dir/fakylab-release-private.asc"
private_key_b64_path="$out_dir/fakylab-release-private.asc.b64"

echo "Exporting public key..."
gpg --armor --output "$public_key_path" --export "$fingerprint"

echo "Exporting private key..."
if [[ -n "$passphrase" ]]; then
    gpg --batch --pinentry-mode loopback --passphrase "$passphrase" \
        --armor --output "$private_key_path" --export-secret-keys "$fingerprint"
else
    echo "GPG may prompt you again to unlock the secret key for export."
    gpg --armor --output "$private_key_path" --export-secret-keys "$fingerprint"
fi

if [[ ! -s "$private_key_path" ]]; then
    echo "Private key export failed; no private key file was created." >&2
    exit 1
fi

base64 "$private_key_path" > "$private_key_b64_path"

cat <<EOF

FakyLab release signing key ready.

UID:          $uid
Fingerprint:  $fingerprint

Created files:
  $public_key_path
  $private_key_path
  $private_key_b64_path

GitHub secrets to add:
  FAKYLAB_GPG_PRIVATE_KEY   -> contents of $private_key_b64_path
  FAKYLAB_GPG_PASSPHRASE    -> the passphrase you used for the key

Verification commands:
  gpg --list-secret-keys --keyid-format LONG "$uid"
  gpg --armor --export "$fingerprint"

Keep the private key files secret.
EOF
