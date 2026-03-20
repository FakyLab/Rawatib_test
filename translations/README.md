# Translations

## Adding a new language — full checklist

1. **Register the language** in `src/utils/LanguageRegistry.cpp` — add one row to
   `s_languages` with the ISO 639-1 code, native name, and RTL flag.

2. **Create the `.ts` file** using Qt's `lupdate` tool:
   ```bash
   lupdate path/to/project -ts translations/rawatib_<code>.ts
   ```
   This generates a skeleton with all source strings marked `unfinished`.

3. **Translate the strings** in the `.ts` file using Qt Linguist or any text editor.

4. **Drop the file here** (`translations/rawatib_<code>.ts`).
   CMake globs `rawatib_*.ts` automatically — no `CMakeLists.txt` edit needed.
   Run `cmake ..` once after adding the file so CMake picks it up.

5. **Optionally add `qtbase_<code>.qm`** (see below) for translated Qt dialog buttons.

6. **Rebuild** — `lrelease` compiles the `.ts` to `.qm` and it gets embedded.

That's it. The first-run dialog and View → Language menu update automatically.

---

## App translation files

| File | Language | Status |
|---|---|---|
| `rawatib_ar.ts` | Arabic (العربية) | ✅ Complete |
| `rawatib_fr.ts` | French (Français) | ✅ Complete |
| `rawatib_tr.ts` | Turkish (Türkçe) | ✅ Complete |
| `rawatib_ko.ts` | Korean (한국어) | ✅ Complete |
| `rawatib_ja.ts` | Japanese (日本語) | ✅ Complete |
| `rawatib_zh.ts` | Chinese Simplified (简体中文) | ✅ Complete |

---

## `qtbase_<code>.qm` — Qt's own dialog button translations

Qt ships its own translation files for standard dialog buttons
(OK, Cancel, Close, Yes, No, etc.).  Without these, those buttons show in English
even when the rest of the app is translated.

**These files are NOT included in this repo** — they are part of Qt itself
(LGPL licensed) and vary between Qt versions.

### How to add them

1. Find the file in your Qt installation:
   - Windows: `C:\Qt\6.x.x\mingw_64\translations\qtbase_<code>.qm`
   - Linux:   `/usr/lib/qt6/translations/qtbase_<code>.qm`
   - macOS:   `~/Qt/6.x.x/macos/translations/qtbase_<code>.qm`

2. Copy it into this `translations/` folder.

3. CMake detects it automatically on the next configure. You will see:
   ```
   -- qtbase_<code>.qm: found in project translations/ folder
   ```

### What happens if a qtbase file is missing

CMake prints a warning but the build succeeds.  On a developer machine with Qt
installed, `main.cpp` falls back to loading it from Qt's installation directory
at runtime.  On an end-user machine without Qt installed, standard dialog buttons
will display in English — everything else translates correctly.

---

## RTL languages

Mark `isRtl = true` in `LanguageRegistry.cpp` for right-to-left scripts.
Currently RTL: Arabic (`ar`).  Future RTL additions: Farsi (`fa`), Hebrew (`he`),
Urdu (`ur`).  All other languages (French, Turkish, Chinese, Japanese, Korean,
etc.) are LTR — leave `isRtl = false`.

The layout direction is set once at startup in `main.cpp` based on the registry.
PrintHelper and ExportHelper derive direction from `QGuiApplication::layoutDirection()`
so they handle any registered language correctly without further changes.

---

## CSV export — always English

CSV files exported by Rawatib always use **English column headers and fixed
strings** regardless of the active UI language.  This is intentional — it ensures
that files exported in French or Turkish can still be re-imported without errors.

Only XLSX and print reports use localized month names (matching the active language).

