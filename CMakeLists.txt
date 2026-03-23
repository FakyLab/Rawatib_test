cmake_minimum_required(VERSION 3.16)
project(Rawatib VERSION 1.0.0 LANGUAGES C CXX)
# Author / Organization: FakyLab
# Cross-platform: Windows, Linux, macOS

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Sanity checks: bundled dependencies ───────────────────────────────────
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/sqlcipher/sqlite3.c")
    message(FATAL_ERROR
        "\nSQLCipher amalgamation not found.\n"
        "Run: ./scripts/fetch_sqlcipher.sh\n"
        "See: third_party/README.md\n")
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/qsqlcipher/qsql_sqlite.cpp")
    message(FATAL_ERROR
        "\nQt SQLite driver sources not found in third_party/qsqlcipher/.\n"
        "Copy from: \$QTDIR/Src/qtbase/src/plugins/sqldrivers/sqlite/\n"
        "See: third_party/README.md\n")
endif()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/qtkeychain/CMakeLists.txt")
    message(FATAL_ERROR
        "\nQt Keychain submodule not found.\n"
        "Run: git submodule update --init --recursive\n"
        "See: third_party/README.md\n")
endif()

# ── Find Qt6 first — subdirectories need it ───────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Sql
    PrintSupport
    Network
    LinguistTools
)

# ── OpenSSL (required by SQLCipher) ───────────────────────────────────────
# qsqlcipher/CMakeLists.txt calls find_package(OpenSSL REQUIRED) itself, so
# this call is idempotent — it reuses the cached result with no side effects.
# The benefit: a clear upfront error if OpenSSL is missing, rather than a
# confusing message buried inside the third_party subdirectory.
#
# Install hints per platform:
#   Ubuntu/Debian : sudo apt install libssl-dev
#   Fedora/RHEL   : sudo dnf install openssl-devel
#   macOS (Homebrew): brew install openssl
#                     export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
#   Windows/MSYS2 : pacman -S mingw-w64-ucrt-x86_64-openssl
find_package(OpenSSL REQUIRED)

# ── Qt Keychain (cross-platform OS credential store) ──────────────────────
set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/qtkeychain)

# ── QXlsx (optional — XLSX export) ────────────────────────────────────────
# If third_party/QXlsx/CMakeLists.txt is present, build it and enable XLSX.
# If not present, ExportHelper gracefully degrades to CSV-only via
# the __has_include guard in ExportHelper.cpp — no build error.
set(QXLSX_AVAILABLE OFF)
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/QXlsx/QXlsx/CMakeLists.txt")
    set(QT_VERSION_MAJOR 6)
    add_subdirectory(third_party/QXlsx/QXlsx)
    set(QXLSX_AVAILABLE ON)
    message(STATUS "QXlsx found — XLSX export enabled")
else()
    message(STATUS "QXlsx not found — XLSX export disabled (CSV only). "
                   "See third_party/README.md to enable.")
endif()

# ── SQLCipher Qt driver plugin (static) ───────────────────────────────────
add_subdirectory(third_party/qsqlcipher)

# Windows executable icon + VERSIONINFO + manifest
if(WIN32)
    # Capture the current year for the copyright string in the RC file.
    string(TIMESTAMP RAWATIB_BUILD_YEAR "%Y")

    # Process app_icon.rc.in → build/app_icon.rc, substituting:
    #   @PROJECT_VERSION_MAJOR/MINOR/PATCH@  from project() above
    #   @PROJECT_VERSION@                    full "1.0.0" string
    #   @RAWATIB_BUILD_YEAR@                 e.g. "2025"
    configure_file(
        ${CMAKE_SOURCE_DIR}/app_icon.rc.in
        ${CMAKE_BINARY_DIR}/app_icon.rc
        @ONLY
    )

    # windres looks for the .ico relative to the .rc file — copy it next to it.
    configure_file(
        ${CMAKE_SOURCE_DIR}/app_icon.ico
        ${CMAKE_BINARY_DIR}/app_icon.ico
        COPYONLY
    )

    set(WIN_ICON ${CMAKE_BINARY_DIR}/app_icon.rc)
endif()

# macOS app bundle icon
# The .icns file lives in resources/ alongside the other app assets.
# Create app_icon.icns from app_icon.png using:
#   macOS: iconutil -c icns app_icon.iconset
#   or any online .png → .icns converter
set(MACOS_ICON "")
if(APPLE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/app_icon.icns")
        set(MACOS_ICON ${CMAKE_SOURCE_DIR}/resources/app_icon.icns)
        set_source_files_properties(${MACOS_ICON}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        message(STATUS "macOS icon: resources/app_icon.icns found")
    else()
        message(STATUS "macOS icon: resources/app_icon.icns not found — bundle will use default icon. "
                       "Drop app_icon.icns into resources/ to set a custom icon.")
    endif()
endif()

set(SOURCES
    src/main.cpp
    src/database/DatabaseManager.cpp
    src/database/AutoBackupManager.cpp
    src/database/Migrations.cpp
    src/utils/AuditLog.cpp
    src/utils/SessionManager.cpp
    src/utils/LanguageRegistry.cpp
    src/models/Employee.cpp
    src/models/AttendanceRecord.cpp
    src/repositories/EmployeeRepository.cpp
    src/repositories/AttendanceRepository.cpp
    src/repositories/DayExceptionRepository.cpp
    src/ui/MainWindow.cpp
    src/ui/EmployeePanel.cpp
    src/ui/AttendanceTab.cpp
    src/ui/SalaryTab.cpp
    src/ui/dialogs/EmployeeDialog.cpp
    src/ui/dialogs/AttendanceDialog.cpp
    src/ui/dialogs/PayMonthDialog.cpp
    src/utils/PrintHelper.cpp
    src/utils/SettingsManager.cpp
    src/utils/PinManager.cpp
    src/utils/CurrencyManager.cpp
    src/utils/ExportHelper.cpp
    src/utils/ImportHelper.cpp
    src/utils/PayrollCalculator.cpp
    src/utils/DeductionPolicy.cpp
    src/utils/UpdateChecker.cpp
    src/repositories/PayrollRulesRepository.cpp
    src/ui/AdvancedTab.cpp
    src/utils/LockPolicy.cpp
    src/utils/EmployeePinManager.cpp
    src/ui/dialogs/LockPolicyDialog.cpp
    src/ui/dialogs/AuditLogDialog.cpp
    src/ui/dialogs/TipsDialog.cpp
    src/ui/dialogs/LicenseDialog.cpp
    src/ui/dialogs/PayrollRuleDialog.cpp
    src/ui/dialogs/PayrollRulesDialog.cpp
    src/ui/dialogs/FirstRunDialog.cpp
    src/ui/dialogs/ExportAllDialog.cpp
    src/ui/dialogs/PinDialog.cpp
    src/ui/dialogs/CurrencyDialog.cpp
    src/ui/dialogs/ImportPreviewDialog.cpp
    src/ui/dialogs/DatabaseUnlockDialog.cpp
)

set(HEADERS
    include/database/DatabaseManager.h
    include/database/AutoBackupManager.h
    include/database/Migrations.h
    include/models/Employee.h
    include/models/AttendanceRecord.h
    include/repositories/EmployeeRepository.h
    include/repositories/AttendanceRepository.h
    include/repositories/DayExceptionRepository.h
    include/ui/MainWindow.h
    include/ui/EmployeePanel.h
    include/ui/AttendanceTab.h
    include/ui/SalaryTab.h
    include/ui/dialogs/EmployeeDialog.h
    include/ui/dialogs/AttendanceDialog.h
    include/ui/dialogs/PayMonthDialog.h
    include/utils/PrintHelper.h
    include/utils/SettingsManager.h
    include/utils/PinManager.h
    include/utils/CurrencyManager.h
    include/utils/ExportHelper.h
    include/utils/ImportHelper.h
    include/utils/PayrollCalculator.h
    include/utils/DeductionPolicy.h
    include/models/PayrollRule.h
    include/repositories/PayrollRulesRepository.h
    include/ui/AdvancedTab.h
    include/utils/LockPolicy.h
    include/utils/EmployeePinManager.h
    include/utils/UpdateChecker.h
    include/utils/ThemeHelper.h
    include/ui/dialogs/LockPolicyDialog.h
    include/ui/dialogs/AuditLogDialog.h
    include/ui/dialogs/TipsDialog.h
    include/ui/dialogs/LicenseDialog.h
    include/utils/AuditLog.h
    include/utils/SessionManager.h
    include/utils/LanguageRegistry.h
    include/ui/dialogs/PayrollRuleDialog.h
    include/ui/dialogs/PayrollRulesDialog.h
    include/ui/dialogs/FirstRunDialog.h
    include/ui/dialogs/ExportAllDialog.h
    include/ui/dialogs/PinDialog.h
    include/ui/dialogs/CurrencyDialog.h
    include/ui/dialogs/ImportPreviewDialog.h
    include/ui/dialogs/DatabaseUnlockDialog.h
)

add_executable(Rawatib
    ${SOURCES}
    ${HEADERS}
    resources.qrc
    ${WIN_ICON}
    ${MACOS_ICON}
)

# ── Translations ───────────────────────────────────────────────────────────
# Discover all rawatib_*.ts files automatically.
# To add a language: drop rawatib_<code>.ts into translations/ — no CMake
# changes needed.  CMake re-globs on the next configure (run cmake again after
# adding a new .ts file so CMake picks it up).
#
# For each language we:
#   1. Compile its .ts → .qm using lrelease.
#   2. Search for the matching qtbase_<code>.qm (project override first,
#      then Qt installation).
#   3. Generate a translations.qrc that embeds every .qm found.

find_program(LRELEASE_EXECUTABLE lrelease
    HINTS ${Qt6_DIR}/../../../bin
)

file(GLOB TS_FILES "${CMAKE_SOURCE_DIR}/translations/rawatib_*.ts")

set(QM_FILES "")              # compiled app .qm files
set(QRC_ENTRIES "")           # lines for the generated .qrc

foreach(TS_FILE ${TS_FILES})
    # Derive language code from filename: rawatib_ar.ts → ar
    get_filename_component(TS_STEM "${TS_FILE}" NAME_WE)   # rawatib_ar
    string(REPLACE "rawatib_" "" LANG_CODE "${TS_STEM}")   # ar

    set(QM_FILE "${CMAKE_BINARY_DIR}/rawatib_${LANG_CODE}.qm")
    list(APPEND QM_FILES "${QM_FILE}")

    add_custom_command(
        OUTPUT  "${QM_FILE}"
        COMMAND "${LRELEASE_EXECUTABLE}" "${TS_FILE}" -qm "${QM_FILE}"
        DEPENDS "${TS_FILE}"
        COMMENT "Compiling translation: rawatib_${LANG_CODE}.qm"
    )

    # Embed the app translation
    string(APPEND QRC_ENTRIES
        "<file alias=\"rawatib_${LANG_CODE}.qm\">${QM_FILE}</file>")

    # Look for the matching Qt base translation
    set(QTBASE_QM "")
    # Map app language codes to Qt's actual qtbase filename suffixes.
    # Qt uses locale-qualified names for some languages (e.g. zh_CN, not zh).
    # Add entries here whenever a new language has a non-matching qtbase name.
    set(QTBASE_LANG_CODE "${LANG_CODE}")
    if(LANG_CODE STREQUAL "zh")
        set(QTBASE_LANG_CODE "zh_CN")
    endif()
    set(QTBASE_FILENAME "qtbase_${QTBASE_LANG_CODE}.qm")

    # 1. Project-local override (developer can drop it into translations/)
    if(EXISTS "${CMAKE_SOURCE_DIR}/translations/${QTBASE_FILENAME}")
        set(QTBASE_QM "${CMAKE_SOURCE_DIR}/translations/${QTBASE_FILENAME}")
        message(STATUS "${QTBASE_FILENAME}: found in project translations/ folder")
    else()
        # 2. Qt installation translations directory
        find_path(QT_TRANSLATIONS_DIR_${LANG_CODE}
            NAMES "${QTBASE_FILENAME}"
            HINTS
                "${Qt6_DIR}/../../../translations"
                "${Qt6Core_DIR}/../../../translations"
                "$ENV{QTDIR}/translations"
            NO_DEFAULT_PATH
        )
        if(QT_TRANSLATIONS_DIR_${LANG_CODE})
            set(QTBASE_QM "${QT_TRANSLATIONS_DIR_${LANG_CODE}}/${QTBASE_FILENAME}")
            message(STATUS "${QTBASE_FILENAME}: found at ${QTBASE_QM}")
        else()
            message(WARNING
                "\n${QTBASE_FILENAME} not found — Qt dialog buttons (OK, Cancel, etc.) "
                "will show in English for language '${LANG_CODE}' on machines without "
                "Qt installed.\nTo fix: copy ${QTBASE_FILENAME} from your Qt "
                "installation's translations/ folder into "
                "${CMAKE_SOURCE_DIR}/translations/ and reconfigure.")
        endif()
    endif()

    if(QTBASE_QM)
        string(APPEND QRC_ENTRIES
            "<file alias=\"${QTBASE_FILENAME}\">${QTBASE_QM}</file>")
    endif()
endforeach()

# Generate the combined translations.qrc
set(TRANS_QRC "${CMAKE_BINARY_DIR}/translations.qrc")
file(WRITE "${TRANS_QRC}"
    "<RCC><qresource prefix=\"/i18n\">${QRC_ENTRIES}</qresource></RCC>")

# Make all .qm files dependencies of the resource compiler
set_source_files_properties("${TRANS_QRC}" PROPERTIES
    OBJECT_DEPENDS "${QM_FILES}"
)

target_sources(Rawatib PRIVATE "${TRANS_QRC}" ${QM_FILES})

target_include_directories(Rawatib PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/qtkeychain/qtkeychain
)

target_link_libraries(Rawatib PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    Qt6::PrintSupport
    Qt6::Network
    QSqlCipherDriverPlugin   # static SQLCipher Qt SQL driver
    qt6keychain              # cross-platform OS credential store
)

# Link QXlsx if available
if(QXLSX_AVAILABLE)
    target_link_libraries(Rawatib PRIVATE QXlsx::QXlsx)
    target_include_directories(Rawatib PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party/QXlsx/QXlsx
    )
endif()

# Static plugin must be explicitly imported at link time
# so Qt's plugin system can find QSQLCIPHER at runtime
target_compile_definitions(Rawatib PRIVATE
    QT_STATICPLUGIN
)

# Tell Qt to import the static QSQLCIPHER plugin
# This replaces the need for a qsqlcipher.dll in plugins/sqldrivers/
set_property(TARGET Rawatib APPEND PROPERTY
    AUTOGEN_TARGET_DEPENDS QSqlCipherDriverPlugin
)

set_target_properties(Rawatib PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE "app_icon.icns"
    MACOSX_BUNDLE_BUNDLE_NAME "Rawatib"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.fakylab.rawatib"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Linux packages should install a lowercase binary that matches the desktop file.
if(UNIX AND NOT APPLE)
    set_target_properties(Rawatib PROPERTIES
        OUTPUT_NAME "rawatib"
    )
endif()

# ── Windows: windeployqt ───────────────────────────────────────────────────
# Copies the Qt DLLs and plugins needed to run Rawatib.exe next to it.
# Only runs in Release builds to avoid cluttering Debug output.
# Without this, the .exe only works on machines that have Qt installed.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS ${Qt6_DIR}/../../../bin
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Rawatib POST_BUILD
            COMMAND ${WINDEPLOYQT_EXECUTABLE}
                --no-translations
                --no-system-d3d-compiler
                --no-compiler-runtime
                "$<TARGET_FILE:Rawatib>"
            COMMENT "Running windeployqt..."
        )
        message(STATUS "windeployqt found — Qt DLLs will be deployed automatically")
    else()
        message(STATUS "windeployqt not found — run it manually after build:\n"
                       "  windeployqt Rawatib.exe")
    endif()
endif()

# ── macOS: macdeployqt ────────────────────────────────────────────────────
# Copies Qt frameworks into the .app bundle so it runs without Qt installed.
# Without this the .app only works on machines that have Qt in the same path.
if(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
        HINTS ${Qt6_DIR}/../../../bin
    )
    if(MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Rawatib POST_BUILD
            COMMAND ${MACDEPLOYQT_EXECUTABLE}
                "$<TARGET_BUNDLE_DIR:Rawatib>"
                -always-overwrite
            COMMENT "Running macdeployqt..."
        )
        message(STATUS "macdeployqt found — Qt frameworks will be bundled automatically")
    else()
        message(STATUS "macdeployqt not found — run it manually after build:\n"
                       "  macdeployqt Rawatib.app")
    endif()
endif()

# ── Linux: install rules ───────────────────────────────────────────────────
# Enables  cmake --install  and proper .deb / .rpm / AppImage packaging.
# Standard GNU paths: binary → /usr/local/bin, icon → hicolor theme,
# .desktop → /usr/local/share/applications.
#
# To install system-wide:
#   cmake --install build/ --prefix /usr/local
# To install to a staging directory (for packaging):
#   cmake --install build/ --prefix /tmp/rawatib-pkg
#
# Note: linuxdeployqt or AppImageTool is still needed to bundle Qt libs
# for distribution to machines without Qt installed. See:
#   https://github.com/linuxdeploy/linuxdeploy
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS Rawatib
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES resources/app_icon.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME rawatib.png
    )
    install(FILES resources/rawatib.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        RENAME com.fakylab.rawatib.desktop
    )
    install(FILES packaging/linux/com.fakylab.rawatib.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
    )
    install(FILES resources/LICENSE.txt resources/THIRD_PARTY_LICENSES.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/rawatib
    )
endif()

# Debian packaging metadata via CPack.
set(CPACK_PACKAGE_NAME "rawatib")
set(CPACK_PACKAGE_VENDOR "FakyLab")
set(CPACK_PACKAGE_CONTACT "fakylab@proton.me")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Employee Attendance & Payroll Manager")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/resources/LICENSE.txt")
set(CPACK_PACKAGE_FILE_NAME "rawatib-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)

