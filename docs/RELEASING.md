# Releasing

This repository now includes two GitHub Actions workflows:

- `build.yml`: compile-checks Windows, Linux, and macOS on push / pull request
- `release.yml`: builds release assets from a `v*` tag

## Release assets

When a tag such as `v1.0.1` is pushed, `release.yml` is designed to publish:

- `Rawatib-<version>-Setup.exe`
- `Rawatib-<version>-windows-portable.zip`
- `rawatib-<version>-x86_64.AppImage`
- `rawatib_<version>_amd64.deb` or the CPack-generated equivalent name
- `rawatib-<version>-macos.dmg`
- `rawatib-<version>-source.tar.gz`
- AUR metadata: `PKGBUILD`, `.SRCINFO`, and `rawatib-<version>-aur.tar.gz`
- `SHA256SUMS`
- Optional `SHA256SUMS.asc` when a GPG key is configured

## Notes

- The source tarball is created from a recursive checkout so bundled submodules are included.
- Linux `.deb` packaging uses CPack metadata from `CMakeLists.txt`.
- AppImage packaging uses `linuxdeploy` and `linuxdeploy-plugin-qt`.
- macOS packaging uses `macdeployqt -dmg`.
- The generated AUR package is a source package, not a prebuilt `-bin` package.

## FakyLab Signing Standard

Recommended reusable signing setup for FakyLab projects:

- Create one long-lived FakyLab GPG release key
- Publish its public key in the GitHub repository and future website
- Sign release checksums, source tarballs, and git tags with that same key

Suggested UID:

- `FakyLab Release Signing <fakylab@proton.me>`

Suggested GitHub secrets for release signing:

- `FAKYLAB_GPG_PRIVATE_KEY`: base64-encoded ASCII-armored private key
- `FAKYLAB_GPG_PASSPHRASE`: passphrase for that key

See [GPG_SIGNING.md](/E:/AI_Proj/Codex/rawatib/docs/GPG_SIGNING.md) for the local setup flow.

This does not replace Apple notarization or Windows Authenticode, but it gives users a consistent integrity verification path across all FakyLab projects.

## macOS Without Apple Signing

Unsigned and unnotarized macOS builds can still run, but Gatekeeper will warn users on first launch.

Typical first-run options are:

- Right-click the app and choose `Open`
- Allow the app in `System Settings > Privacy & Security`

This is acceptable for testers and advanced users, but broad public macOS distribution works much better with Apple Developer signing and notarization.

## Important Arch Linux distinction

The AUR is not the official Arch Linux repositories.

- AUR: community-maintained package recipes
- Official Arch repos: curated by Arch maintainers with stricter packaging and policy requirements

Publishing a clean AUR package is a good first step, but it does not by itself qualify the app for the official repos.
