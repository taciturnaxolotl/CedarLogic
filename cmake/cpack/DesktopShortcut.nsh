; "Create a Desktop shortcut" on the finish page, borrowing the readme checkbox.
; CPack's own shortcut box only appears alongside its PATH options.

!ifndef DESKTOP_SHORTCUT_NSH
!define DESKTOP_SHORTCUT_NSH

!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Create a Desktop shortcut"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION CreateDesktopShortcut

Function CreateDesktopShortcut
  CreateShortCut "$DESKTOP\CedarLogic.lnk" "$INSTDIR\CedarLogic.exe"
FunctionEnd

!endif
