; ============================================================================
;  Rawatib Installer — rawatib_installer.nsi
;  NSIS 3.x required
;
;  Handles:
;    - Fresh install
;    - Upgrade   (same or newer version over older)
;    - Downgrade (older version over newer — user confirmed)
;    - Uninstall (from Add/Remove Programs or installer re-run)
;
;  Usage:
;    makensis rawatib_installer.nsi
;
;  Expects the built release files in a folder called "release\" next to
;  this script — i.e. the same directory layout that windeployqt produces:
;
;    release\
;      Rawatib.exe
;      Qt6Core.dll
;      Qt6Widgets.dll
;      Qt6Sql.dll
;      Qt6PrintSupport.dll
;      Qt6Gui.dll
;      Qt6Network.dll
;      libssl-3-x64.dll         (or libssl*.dll — OpenSSL)
;      libcrypto-3-x64.dll      (or libcrypto*.dll — OpenSSL)
;      libgcc_s_seh-1.dll       (MinGW runtime)
;      libstdc++-6.dll
;      libwinpthread-1.dll
;      platforms\
;        qwindows.dll
;      styles\
;        qwindowsvistastyle.dll
;      imageformats\
;        ...
;      tls\
;        ...
;      sqldrivers\
;        ...           (qsqlcipher is static — no file here)
;
;  Output:  Rawatib-1.0.0-Setup.exe
; ============================================================================

; ── Compiler flags ───────────────────────────────────────────────────────────
!include "MUI2.nsh"          ; Modern UI 2
!include "LogicLib.nsh"      ; ${If} ${AndIf} etc.
!include "WinVer.nsh"        ; ${AtLeastWin10}
!include "x64.nsh"           ; ${RunningX64}
!include "WordFunc.nsh"      ; VersionCompare
!insertmacro VersionCompare

; ── App metadata — update these when bumping the version ────────────────────
!define APP_NAME         "Rawatib"
!define APP_NAME_AR      "رواتب"
!define APP_DESCRIPTION  "Employee Attendance & Payroll Manager"
!define APP_VERSION      "1.0.0"
!define APP_PUBLISHER    "FakyDev"
!define APP_URL          "https://github.com/FakyLab/Rawatib"
!define APP_EXE          "Rawatib.exe"
!define APP_ICON         "app_icon.ico"   ; .ico file in project root
!define INSTALL_DIR      "$PROGRAMFILES64\${APP_NAME}"
!define UNINSTALL_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
!define SETTINGS_KEY     "Software\FakyDev\Rawatib"    ; QSettings registry path
!define KEYCHAIN_SERVICE "Rawatib"                     ; Windows Credential Manager target
!define APPDATA_DIR      "$APPDATA\FakyDev\Rawatib"    ; QStandardPaths::AppDataLocation
!define MUTEX_NAME       "RawatibInstallerMutex"
!define SOURCE_DIR       "release"                     ; folder with built files

; ── Installer output ─────────────────────────────────────────────────────────
Name                "${APP_NAME} - ${APP_DESCRIPTION}"
OutFile             "${APP_NAME}-${APP_VERSION}-Setup.exe"
InstallDir          "${INSTALL_DIR}"
InstallDirRegKey    HKLM "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor       /SOLID lzma
SetCompressorDictSize 64

; ── Version info embedded in the installer exe itself ────────────────────────
VIProductVersion    "${APP_VERSION}.0"
VIAddVersionKey     "ProductName"      "${APP_NAME}"
VIAddVersionKey     "ProductVersion"   "${APP_VERSION}"
VIAddVersionKey     "CompanyName"      "${APP_PUBLISHER}"
VIAddVersionKey     "FileDescription"  "${APP_NAME} Installer"
VIAddVersionKey     "FileVersion"      "${APP_VERSION}.0"
VIAddVersionKey     "LegalCopyright"   "Copyright (C) 2026 ${APP_PUBLISHER}"

; ── Modern UI configuration ──────────────────────────────────────────────────
!define MUI_ABORTWARNING
!define MUI_ICON   "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"

; Header / welcome images — comment out if you don't have custom images
; !define MUI_WELCOMEFINISHPAGE_BITMAP  "installer_assets\welcome.bmp"   ; 164x314
; !define MUI_HEADERIMAGE
; !define MUI_HEADERIMAGE_BITMAP        "installer_assets\header.bmp"    ; 150x57

!define MUI_WELCOMEPAGE_TITLE         "Welcome to ${APP_NAME} ${APP_VERSION} Setup"
!define MUI_WELCOMEPAGE_TEXT          "This wizard will guide you through the installation of ${APP_NAME}.$\r$\n$\r$\n${APP_DESCRIPTION}.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_TITLE          "Installation Complete"
!define MUI_FINISHPAGE_RUN            "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT       "Launch ${APP_NAME} now"
!define MUI_FINISHPAGE_LINK           "Visit ${APP_NAME} on GitHub"
!define MUI_FINISHPAGE_LINK_LOCATION  "${APP_URL}"

; ── Installer pages ──────────────────────────────────────────────────────────
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE         "resources\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; ── Uninstaller pages ────────────────────────────────────────────────────────
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; ── Language ─────────────────────────────────────────────────────────────────
!insertmacro MUI_LANGUAGE "English"

; ============================================================================
;  Macros
; ============================================================================

; Kill running instance and wait for it to exit
!macro KillRunningInstance
    DetailPrint "Checking for running ${APP_NAME} instance..."
    FindProcDLL::FindProc "${APP_EXE}"
    ${If} $R0 == 1
        MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
            "${APP_NAME} is currently running.$\r$\n$\r$\nPlease save your work and close it, then click OK to continue." \
            IDOK +2
        Quit
        ; Wait up to 10 seconds for the process to exit
        ${ForEach} $R1 0 20 + 1
            Sleep 500
            FindProcDLL::FindProc "${APP_EXE}"
            ${If} $R0 == 0
                ${Break}
            ${EndIf}
        ${Next}
        FindProcDLL::FindProc "${APP_EXE}"
        ${If} $R0 == 1
            MessageBox MB_OK|MB_ICONSTOP \
                "${APP_NAME} could not be closed automatically.$\r$\nPlease close it manually and run the installer again."
            Quit
        ${EndIf}
    ${EndIf}
!macroend

; Read the installed version from the uninstall registry key
; Result in $R0 (empty string if not installed)
!macro GetInstalledVersion
    ReadRegStr $R0 HKLM "${UNINSTALL_KEY}" "DisplayVersion"
!macroend

; ============================================================================
;  .onInit — runs before any page is shown
; ============================================================================
Function .onInit

    ; Prevent two instances of the installer running simultaneously
    System::Call 'kernel32::CreateMutex(p 0, i 1, t "${MUTEX_NAME}") p .r1 ?e'
    Pop $R0
    ${If} $R0 = 183   ; ERROR_ALREADY_EXISTS
        MessageBox MB_OK|MB_ICONSTOP "The ${APP_NAME} installer is already running."
        Quit
    ${EndIf}

    ; Require 64-bit Windows
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP \
            "${APP_NAME} requires a 64-bit version of Windows.$\r$\nThis installer cannot continue."
        Quit
    ${EndIf}

    ; Require Windows 10 or later
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP \
            "${APP_NAME} requires Windows 10 or later.$\r$\nThis installer cannot continue."
        Quit
    ${EndIf}

    ; ── Version check — handle upgrade and downgrade ──────────────────────
    !insertmacro GetInstalledVersion
    ${If} $R0 != ""
        ; Something is already installed — compare versions
        ${VersionCompare} "${APP_VERSION}" "$R0" $R1
        ; $R1:  0 = equal, 1 = new > installed, 2 = new < installed

        ${If} $R1 = 0
            ; Same version already installed
            MessageBox MB_YESNO|MB_ICONQUESTION \
                "${APP_NAME} ${APP_VERSION} is already installed.$\r$\n$\r$\nDo you want to reinstall it?" \
                IDYES +2
            Quit

        ${ElseIf} $R1 = 1
            ; Upgrading — inform user, continue automatically
            MessageBox MB_YESNO|MB_ICONINFORMATION \
                "An older version of ${APP_NAME} ($R0) is installed.$\r$\n$\r$\nThis will upgrade it to version ${APP_VERSION}.$\r$\n$\r$\nYour data and settings will be preserved.$\r$\n$\r$\nContinue?" \
                IDYES +2
            Quit

        ${ElseIf} $R1 = 2
            ; Downgrading — warn explicitly
            MessageBox MB_YESNO|MB_ICONEXCLAMATION \
                "A newer version of ${APP_NAME} ($R0) is already installed.$\r$\n$\r$\nDowngrading to version ${APP_VERSION} is not recommended.$\r$\n$\r$\nYour data will be preserved, but some features may not work correctly with an older version.$\r$\n$\r$\nAre you sure you want to downgrade?" \
                IDYES +2
            Quit
        ${EndIf}
    ${EndIf}

FunctionEnd

; ============================================================================
;  .onInstFailed — runs if installation is aborted or fails
; ============================================================================
Function .onInstFailed
    MessageBox MB_OK|MB_ICONSTOP \
        "Installation of ${APP_NAME} was not completed.$\r$\nIf any files were partially installed, they may have been left in $INSTDIR."
FunctionEnd

; ============================================================================
;  Main install section
; ============================================================================
Section "Install" SecInstall

    SectionIn RO   ; Required — user cannot deselect

    ; Kill any running instance before overwriting files
    !insertmacro KillRunningInstance

    ; Set output path
    SetOutPath "$INSTDIR"
    SetOverwrite on

    ; ── Main executable ───────────────────────────────────────────────────
    File "${SOURCE_DIR}\${APP_EXE}"

    ; ── Qt runtime DLLs ───────────────────────────────────────────────────
    File /nonfatal "${SOURCE_DIR}\Qt6Core.dll"
    File /nonfatal "${SOURCE_DIR}\Qt6Gui.dll"
    File /nonfatal "${SOURCE_DIR}\Qt6Widgets.dll"
    File /nonfatal "${SOURCE_DIR}\Qt6Sql.dll"
    File /nonfatal "${SOURCE_DIR}\Qt6PrintSupport.dll"
    File /nonfatal "${SOURCE_DIR}\Qt6Network.dll"
    File /nonfatal "${SOURCE_DIR}\Qt6Svg.dll"

    ; ── MinGW runtime ─────────────────────────────────────────────────────
    File /nonfatal "${SOURCE_DIR}\libgcc_s_seh-1.dll"
    File /nonfatal "${SOURCE_DIR}\libstdc++-6.dll"
    File /nonfatal "${SOURCE_DIR}\libwinpthread-1.dll"

    ; ── OpenSSL DLLs ──────────────────────────────────────────────────────
    ; Wildcard catches all naming conventions across OpenSSL versions:
    ;   libssl-3-x64.dll   / libcrypto-3-x64.dll   (Qt installer component)
    ;   libssl-3.dll       / libcrypto-3.dll        (MSYS2 OpenSSL 3)
    ;   libssl-1_1-x64.dll / libcrypto-1_1-x64.dll (OpenSSL 1.1)
    File /nonfatal "${SOURCE_DIR}\libssl*.dll"
    File /nonfatal "${SOURCE_DIR}\libcrypto*.dll"

    ; ── Qt platform plugin ────────────────────────────────────────────────
    SetOutPath "$INSTDIR\platforms"
    File "${SOURCE_DIR}\platforms\qwindows.dll"

    ; ── Qt styles ─────────────────────────────────────────────────────────
    SetOutPath "$INSTDIR\styles"
    File /nonfatal "${SOURCE_DIR}\styles\*.dll"

    ; ── Qt image format plugins ───────────────────────────────────────────
    SetOutPath "$INSTDIR\imageformats"
    File /nonfatal "${SOURCE_DIR}\imageformats\*.dll"

    ; ── Qt TLS backend plugins ────────────────────────────────────────────
    SetOutPath "$INSTDIR\tls"
    File /nonfatal "${SOURCE_DIR}\tls\*.dll"

    ; ── Qt network information plugins ────────────────────────────────────
    SetOutPath "$INSTDIR\networkinformation"
    File /nonfatal "${SOURCE_DIR}\networkinformation\*.dll"

    ; ── Qt generic plugins ────────────────────────────────────────────────
    SetOutPath "$INSTDIR\generic"
    File /nonfatal "${SOURCE_DIR}\generic\*.dll"

    ; ── Reset output path back to install root ────────────────────────────
    SetOutPath "$INSTDIR"

    ; ── Write uninstall registry keys ─────────────────────────────────────
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "DisplayName"          "${APP_NAME} - ${APP_DESCRIPTION}"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "DisplayVersion"        "${APP_VERSION}"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "Publisher"             "${APP_PUBLISHER}"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "URLInfoAbout"          "${APP_URL}"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "InstallLocation"       "$INSTDIR"
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "UninstallString"       '"$INSTDIR\Uninstall.exe"'
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "QuietUninstallString"  '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr   HKLM "${UNINSTALL_KEY}" "DisplayIcon"           '"$INSTDIR\${APP_EXE}",0'
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify"              1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair"              1

    ; Estimated install size (kilobytes) — update if the build size changes significantly
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize"         65536   ; ~64 MB

    ; ── Write uninstaller ─────────────────────────────────────────────────
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; ── Start Menu shortcut ───────────────────────────────────────────────
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
                    "$INSTDIR\${APP_EXE}" "" \
                    "$INSTDIR\${APP_EXE}" 0 \
                    SW_SHOWNORMAL "" \
                    "${APP_DESCRIPTION}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" \
                    "$INSTDIR\Uninstall.exe" "" \
                    "$INSTDIR\Uninstall.exe" 0

    ; ── Desktop shortcut (optional — user can remove it) ─────────────────
    CreateShortcut  "$DESKTOP\${APP_NAME}.lnk" \
                    "$INSTDIR\${APP_EXE}" "" \
                    "$INSTDIR\${APP_EXE}" 0 \
                    SW_SHOWNORMAL "" \
                    "${APP_DESCRIPTION}"

SectionEnd

; ============================================================================
;  Uninstall section
; ============================================================================
Section "Uninstall"

    ; Kill any running instance before removing files
    !insertmacro KillRunningInstance

    ; ── Ask about user data ───────────────────────────────────────────────
    ; The database and settings live in AppData — never delete silently.
    ; Only offer to remove them if the user explicitly asks.
    MessageBox MB_YESNO|MB_ICONQUESTION \
        "Do you want to remove your ${APP_NAME} data and settings?$\r$\n$\r$\nThis includes:$\r$\n  - The database (all employees, attendance records, payroll rules)$\r$\n  - Auto-backups$\r$\n  - Application settings$\r$\n$\r$\nIf you plan to reinstall later, click No to keep your data.$\r$\n$\r$\nThis cannot be undone." \
        IDNO skip_user_data

    ; User chose to delete data — remove AppData directory entirely
    RMDir /r "${APPDATA_DIR}"

    ; Remove QSettings registry key (language, session timeout, etc.)
    DeleteRegKey HKCU "${SETTINGS_KEY}"

    ; Remove Windows Credential Manager entry (encrypted database key)
    ; We use cmdkey to delete the stored credential gracefully.
    ; The credential is stored under target name matching KEYCHAIN_SERVICE.
    ExecWait 'cmdkey /delete:${KEYCHAIN_SERVICE}' $R0
    ; Ignore $R0 — it's fine if the credential doesn't exist

    skip_user_data:

    ; ── Remove installed files ────────────────────────────────────────────

    ; Main executable and uninstaller
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\Uninstall.exe"

    ; Qt DLLs
    Delete "$INSTDIR\Qt6Core.dll"
    Delete "$INSTDIR\Qt6Gui.dll"
    Delete "$INSTDIR\Qt6Widgets.dll"
    Delete "$INSTDIR\Qt6Sql.dll"
    Delete "$INSTDIR\Qt6PrintSupport.dll"
    Delete "$INSTDIR\Qt6Network.dll"
    Delete "$INSTDIR\Qt6Svg.dll"

    ; MinGW runtime
    Delete "$INSTDIR\libgcc_s_seh-1.dll"
    Delete "$INSTDIR\libstdc++-6.dll"
    Delete "$INSTDIR\libwinpthread-1.dll"

    ; OpenSSL DLLs — wildcard covers all naming conventions
    Delete "$INSTDIR\libssl*.dll"
    Delete "$INSTDIR\libcrypto*.dll"

    ; Qt plugin subdirectories — remove all DLLs then the directories
    Delete "$INSTDIR\platforms\*.dll"
    RMDir  "$INSTDIR\platforms"

    Delete "$INSTDIR\styles\*.dll"
    RMDir  "$INSTDIR\styles"

    Delete "$INSTDIR\imageformats\*.dll"
    RMDir  "$INSTDIR\imageformats"

    Delete "$INSTDIR\tls\*.dll"
    RMDir  "$INSTDIR\tls"

    Delete "$INSTDIR\networkinformation\*.dll"
    RMDir  "$INSTDIR\networkinformation"

    Delete "$INSTDIR\generic\*.dll"
    RMDir  "$INSTDIR\generic"

    Delete "$INSTDIR\sqldrivers\*.dll"
    RMDir  "$INSTDIR\sqldrivers"

    ; Remove install directory if empty
    RMDir "$INSTDIR"

    ; ── Remove shortcuts ──────────────────────────────────────────────────
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    ; ── Remove uninstall registry key ────────────────────────────────────
    DeleteRegKey HKLM "${UNINSTALL_KEY}"

SectionEnd

; ============================================================================
;  Uninstaller .onInit — runs when uninstall.exe is launched directly
; ============================================================================
Function un.onInit

    ; Confirm uninstall
    MessageBox MB_YESNO|MB_ICONQUESTION \
        "Are you sure you want to uninstall ${APP_NAME} ${APP_VERSION}?" \
        IDYES +2
    Quit

FunctionEnd

; ============================================================================
;  un.onUninstSuccess — runs after uninstall completes
; ============================================================================
Function un.onUninstSuccess
    MessageBox MB_OK|MB_ICONINFORMATION \
        "${APP_NAME} has been successfully uninstalled."
FunctionEnd
