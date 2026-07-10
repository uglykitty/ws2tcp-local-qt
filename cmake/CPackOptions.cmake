# CPack exposes the desktop shortcut checkbox when the install-options page is
# enabled, but hardcodes it to unchecked in its NSIS template.
if(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_NSIS_DEFINES [=[
!define MUI_CUSTOMFUNCTION_GUIINIT SetDesktopShortcutDefault
Function SetDesktopShortcutDefault
    WriteINIStr "$PLUGINSDIR\NSIS.InstallOptions.ini" "Field 5" "State" "1"
FunctionEnd
]=])
endif()
