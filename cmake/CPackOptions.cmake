# CPack exposes the desktop shortcut checkbox when the install-options page is
# enabled, but hardcodes it to unchecked in its NSIS template.
if(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_NSIS_DEFINES [=[
!include "nsDialogs.nsh"
!include "LogicLib.nsh"

Var KeepUserSettingsCheckbox
Var KeepUserSettings
; Set only by un.UserSettingsPageLeave below, i.e. only when a user actually
; ran the uninstaller interactively and reached that page. NSIS variables
; default to an empty string, so a silent run -- including the one
; CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL performs before an upgrade,
; where the page is never shown -- leaves this unset. The uninstall commands
; below key off this flag rather than IfSilent, so "settings were explicitly
; requested to be cleared" is what decides the behavior, not silence.
Var UserSettingsPageShown

!define MUI_CUSTOMFUNCTION_GUIINIT SetDesktopShortcutDefault
Function SetDesktopShortcutDefault
    WriteINIStr "$PLUGINSDIR\NSIS.InstallOptions.ini" "Field 5" "State" "1"
FunctionEnd

UninstPage custom un.UserSettingsPage un.UserSettingsPageLeave

Function un.UserSettingsPage
    !insertmacro MUI_HEADER_TEXT \
        "User Settings" \
        "Choose whether to keep your user settings"

    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateCheckbox} 0 20u 100% 12u "Keep user settings"
    Pop $KeepUserSettingsCheckbox
    ${NSD_Uncheck} $KeepUserSettingsCheckbox

    nsDialogs::Show
FunctionEnd

Function un.UserSettingsPageLeave
    ${NSD_GetState} $KeepUserSettingsCheckbox $KeepUserSettings
    StrCpy $UserSettingsPageShown "1"
FunctionEnd
]=])

    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
; Ask any running instance to quit and wait for it to actually exit before
; files are touched, so the upgrade/uninstall does not race a locked exe.
; This runs unconditionally (including on the silent uninstall that
; CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL performs before an upgrade),
; unlike the user-settings prompt below which only makes sense interactively.
ExecWait '"$INSTDIR\bin\ws2tcp-local-qt.exe" --quit-running-instance' $0
${If} $0 != 0
    IfSilent skip_quit_warning
    MessageBox MB_ICONEXCLAMATION|MB_OK \
        "ws2tcp-local could not be closed automatically (error code $0). Please close it manually to avoid leftover files."
    skip_quit_warning:
${EndIf}

${If} $UserSettingsPageShown == "1"
${AndIf} $KeepUserSettings != ${BST_CHECKED}
    ExecWait '"$INSTDIR\bin\ws2tcp-local-qt.exe" --clear-user-settings' $0
    ${If} $0 != 0
        MessageBox MB_ICONEXCLAMATION|MB_OK \
            "User settings could not be cleared (error code $0)."
    ${EndIf}
${EndIf}
]=])
endif()
