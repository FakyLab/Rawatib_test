# GPG Release Signing

This project supports optional release-signing for `SHA256SUMS` in GitHub Actions.

## Goal

Use one reusable signing identity across FakyLab projects:

- `FakyLab Release Signing <fakylab@proton.me>`

That gives you:

- signed release checksums
- signed source releases
- a consistent public verification identity across projects

## 1. Generate the key locally

Run:

```bash
bash scripts/setup-fakylab-gpg.sh
```

If you want to avoid pinentry prompts during generation/export, you can provide the passphrase up front:

```bash
FAKYLAB_GPG_PASSPHRASE='your-passphrase' bash scripts/setup-fakylab-gpg.sh
```

By default this creates:

- an Ed25519 signing key
- UID `FakyLab Release Signing <fakylab@proton.me>`
- output files in `dist/gpg/`

Generated files:

- `dist/gpg/fakylab-release-public.asc`
- `dist/gpg/fakylab-release-private.asc`
- `dist/gpg/fakylab-release-private.asc.b64`

If the directory is created but stays empty, the key generation or secret-key export likely failed locally. In that case run:

```bash
gpg --list-secret-keys --keyid-format LONG
```

If the FakyLab key exists already, rerun the script and it will reuse it.

## 2. Add GitHub secrets

In the GitHub repository settings, add:

- `FAKYLAB_GPG_PRIVATE_KEY`
  Use the full contents of `dist/gpg/fakylab-release-private.asc.b64`
- `FAKYLAB_GPG_PASSPHRASE`
  Use the passphrase for the key

Once those secrets exist, the release workflow will automatically publish:

- `SHA256SUMS`
- `SHA256SUMS.asc`

## 3. Publish the public key

Make the public key available to users:

- attach `dist/gpg/fakylab-release-public.asc` to a release, or
- commit a copy under a public `keys/` folder later, or
- publish it on your future FakyLab website

## 4. Verify a release

Example:

```bash
gpg --import fakylab-release-public.asc
gpg --verify SHA256SUMS.asc SHA256SUMS
sha256sum -c SHA256SUMS
```

## Notes

- This improves authenticity and integrity verification.
- It does not replace Apple notarization on macOS.
- It does not replace Windows Authenticode signing.
- Keep `fakylab-release-private.asc` and `fakylab-release-private.asc.b64` secret.
