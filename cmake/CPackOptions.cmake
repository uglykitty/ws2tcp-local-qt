# CPack exposes the desktop shortcut checkbox when the install-options page is
# enabled, but hardcodes it to unchecked in its NSIS template.
if(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_NSIS_DEFINES [=[
!include "nsDialogs.nsh"
!include "LogicLib.nsh"

Var KeepUserSettingsCheckbox
Var KeepUserSettings

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
FunctionEnd
]=])

    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
; A silent uninstall never shows the custom un.UserSettingsPage above, so
; $KeepUserSettings is left at its unset default (not ${BST_CHECKED}) and
; the check below would wipe user settings unconditionally. That is exactly
; what happens on every upgrade: CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL
; silently runs the previous version's uninstaller before installing the
; new one. Skip the clear entirely when silent; only an interactive,
; user-driven uninstall should offer to wipe settings.
IfSilent skip_clear_user_settings
${If} $KeepUserSettings != ${BST_CHECKED}
    ExecWait '"$INSTDIR\bin\ws2tcp-local-qt.exe" --clear-user-settings' $0
    ${If} $0 != 0
        MessageBox MB_ICONEXCLAMATION|MB_OK \
            "User settings could not be cleared (error code $0)."
    ${EndIf}
${EndIf}
skip_clear_user_settings:
]=])
endif()
