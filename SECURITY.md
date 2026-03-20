# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| 1.0.x   | ✅ Yes    |

## Reporting a vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Instead, report them privately by emailing the maintainer or using [GitHub's private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing/privately-reporting-a-security-vulnerability) feature on this repository.

Include as much detail as you can:
- A description of the vulnerability and its potential impact
- Steps to reproduce or a proof-of-concept if possible
- The version of Rawatib affected

You can expect an acknowledgement within 72 hours and a resolution or status update within 14 days.

## Security model and known limitations

- The database is always encrypted with AES-256 (SQLCipher). Without the admin PIN the file cannot be opened.
- When no admin PIN is set, a compiled-in fallback key is used. This fallback key is visible in the open-source code — it protects against casual file copying only, not against an attacker who has read the source and has full machine access. **Set an admin PIN for real protection.**
- The admin PIN drives both authentication and SQLCipher key derivation via PBKDF2-SHA256 (100,000 iterations).
- The derived key is stored in the OS keychain (Windows Credential Manager, macOS Keychain, Linux Secret Service). It is never written to disk in plaintext.
- All security-sensitive settings (PIN hash, lock policy, rate-limit state) live inside the encrypted database, not in the system registry or plist files.
