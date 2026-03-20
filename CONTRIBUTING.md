# Contributing to Rawatib

Thank you for your interest in contributing! Here is everything you need to get started.

---

## Ways to contribute

- **Bug reports** — open an issue using the Bug Report template
- **Feature requests** — open an issue using the Feature Request template
- **Translations** — add or improve a language (see below)
- **Code** — fix a bug or implement a feature; open an issue first for anything non-trivial so we can discuss the approach before you invest time writing it

---

## Development setup

1. **Clone the repository and initialise submodules**
   ```bash
   git clone https://github.com/FakyLab/Rawatib.git
   cd Rawatib
   git submodule update --init --recursive
   ```

2. **Fetch the SQLCipher amalgamation** (requires `git` and `tclsh`)
   ```bash
   bash scripts/fetch_sqlcipher.sh
   ```

3. **Copy the qsqlcipher Qt SQL driver files** from your Qt source — see [Third-Party Setup](README.md#third-party-setup) in the README.

4. **Build**
   ```bash
   # Linux / macOS
   chmod +x build.sh && ./build.sh

   # Windows — double-click build.bat, or run it from a terminal
   ```

See [Building from Source](README.md#building-from-source) in the README for platform-specific details and manual build instructions.

---

## Code style

- **C++17**, Qt 6, CMake
- Follow the existing style: 4-space indentation, `camelCase` for variables and functions, `PascalCase` for types
- Keep the layered architecture intact: UI → Utilities → Repositories → Database. The UI layer must not touch the database directly.
- Wrap all user-visible strings in `tr()` with the appropriate translation context
- Add prepared statements for all SQL — no string concatenation in queries
- Round monetary values to 2 decimal places using `std::round(x * 100.0) / 100.0`

---

## Adding a translation

1. Add one row to `s_languages` in `src/utils/LanguageRegistry.cpp`
2. Run `lupdate` to generate the `.ts` skeleton
3. Translate the strings in Qt Linguist or any text editor
4. Drop the `.ts` file in `translations/` — CMake picks it up automatically
5. See `translations/README.md` for the full checklist

---

## Submitting a pull request

1. Fork the repository and create a branch from `main`
2. Make your changes; keep commits focused and descriptive
3. Test on at least one platform
4. Open a pull request with a clear description of what changed and why

---

## Reporting a security issue

Please do **not** open a public issue for security vulnerabilities. See [SECURITY.md](SECURITY.md) for the responsible disclosure process.
