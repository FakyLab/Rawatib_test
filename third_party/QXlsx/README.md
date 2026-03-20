# QXlsx

QXlsx is a C++ library for reading and writing Excel files (.xlsx) without
requiring Microsoft Excel or any other Office suite to be installed.

- License: MIT
- Repository: https://github.com/QtExcel/QXlsx
- Version: 1.4.6

## Setup

Clone or copy the QXlsx source into this directory so the structure is:

```
third_party/QXlsx/
    CMakeLists.txt       ← provided by QXlsx repo
    QXlsx/
        xlsxdocument.h
        xlsxdocument.cpp
        ... (all source files)
```

Then run from the project root:

```bash
git submodule add https://github.com/QtExcel/QXlsx.git third_party/QXlsx
git submodule update --init --recursive
```

Or manually:
```bash
git clone https://github.com/QtExcel/QXlsx.git third_party/QXlsx
```

No additional system dependencies needed — QXlsx uses only Qt.

## Why QXlsx

- MIT license (compatible with GPLv3)
- Qt-native API — uses Qt types throughout
- No external dependencies beyond Qt
- Supports cell formatting, colors, formulas, merged cells, sheet names
- Actively maintained
