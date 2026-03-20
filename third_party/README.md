# third_party/

This directory contains bundled dependencies so contributors only need Qt 6
and OpenSSL installed — nothing else.

---

## sqlcipher/   ← YOU MUST POPULATE THIS

SQLCipher is not checked into this repository due to its size.
You must generate the amalgamation files before building.

### Getting the SQLCipher amalgamation

**Option 1 — Use the helper script (recommended)**

From the project root:
```bash
./scripts/fetch_sqlcipher.sh
```
This clones SQLCipher, generates the amalgamation, and places
`sqlite3.c` and `sqlite3.h` in `third_party/sqlcipher/`.

**Option 2 — Manual steps**

```bash
# Prerequisites: tclsh (usually available on Linux/macOS, install ActiveTcl on Windows)
git clone https://github.com/sqlcipher/sqlcipher.git /tmp/sqlcipher
cd /tmp/sqlcipher
./configure --enable-tempstore=yes \
            CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL" \
            LDFLAGS="-lcrypto"
make sqlite3.c   # generates the amalgamation
cp sqlite3.c sqlite3.h /path/to/AttendanceApp/third_party/sqlcipher/
```

**On Windows (MSYS2/MinGW):**
```bash
pacman -S tcl  # install tclsh
cd /tmp
git clone https://github.com/sqlcipher/sqlcipher.git
cd sqlcipher
./configure --enable-tempstore=yes \
            CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL" \
            LDFLAGS="-lcrypto"
make sqlite3.c
cp sqlite3.c sqlite3.h /path/to/AttendanceApp/third_party/sqlcipher/
```

### Expected files after setup
```
third_party/sqlcipher/
    sqlite3.c    (~8 MB amalgamation)
    sqlite3.h    (~600 KB header)
```

---

## qsqlcipher/  ← already complete, no action needed

Contains the Qt SQL driver plugin that wraps SQLCipher.
Built automatically as part of the Rawatib CMake build.
Source files `qsql_sqlite.cpp`, `qsql_sqlite_p.h`, `qsql_sqlite_vfs.cpp`,
`qsql_sqlite_vfs_p.h` are copied from Qt 6's own SQLite driver source
(found in `$QTDIR/Src/qtbase/src/plugins/sqldrivers/sqlite/`).

You need to copy these four files from your Qt installation:
```
$QTDIR/Src/qtbase/src/plugins/sqldrivers/sqlite/qsql_sqlite.cpp
$QTDIR/Src/qtbase/src/plugins/sqldrivers/sqlite/qsql_sqlite_p.h
$QTDIR/Src/qtbase/src/plugins/sqldrivers/sqlite/qsql_sqlite_vfs.cpp
$QTDIR/Src/qtbase/src/plugins/sqldrivers/sqlite/qsql_sqlite_vfs_p.h
```
Into `third_party/qsqlcipher/`.

These files are part of Qt's LGPL source and cannot be redistributed
in this repository directly, but are included in every Qt installation.

---

## qtkeychain/  ← git submodule

Populated automatically via:
```bash
git submodule update --init --recursive
```

Or manually:
```bash
git clone https://github.com/frankosterfeld/qtkeychain.git third_party/qtkeychain
```

### Linux additional dependency
```bash
sudo apt install libsecret-1-dev   # Ubuntu/Debian
sudo dnf install libsecret-devel   # Fedora
```

### macOS / Windows
No additional dependencies needed.

---

## QXlsx/  ← optional, git submodule

Required for XLSX export. If absent, export falls back to CSV-only — the
app builds and runs without it.

Populated via:
```bash
git submodule add https://github.com/QtExcel/QXlsx.git third_party/QXlsx
git submodule update --init --recursive
```

Or manually:
```bash
git clone https://github.com/QtExcel/QXlsx.git third_party/QXlsx
```

### Expected structure after setup
The QXlsx repo keeps its library source in a `QXlsx/` subfolder:
```
third_party/QXlsx/
    QXlsx/
        CMakeLists.txt    ← CMake looks here
        xlsxdocument.h
        xlsxdocument.cpp
        ...
    HelloWorld/
    README.md
    ...
```

### No additional dependencies
QXlsx uses only Qt — no system libraries needed.

### License
MIT — compatible with Rawatib's GPLv3.
