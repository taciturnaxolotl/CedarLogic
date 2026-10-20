/*
_____________________________________________________________________________

                         Offer a desktop shortcut
_____________________________________________________________________________

 The installer never offered a desktop shortcut, and the one "Browse" button
 it did show was the install folder, so students who wanted an icon on the
 desktop installed the whole program there.

 CPack fixes the page order and only shows its own shortcut checkbox together
 with the PATH options, which nobody here needs. The finish page's "show
 readme" checkbox is the usual way around that: relabelled, checked by
 default, and wired to a function that makes the shortcut instead.

 Usage, via CPACK_NSIS_DEFINES so it lands before the pages are declared:
   !include "DesktopShortcut.nsh"
_____________________________________________________________________________
*/

!ifndef DESKTOP_SHORTCUT_NSH
!define DESKTOP_SHORTCUT_NSH

!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Create a Desktop shortcut"
!define MUI_FINISHPAGE_SHOWREADME_FUNCTION CreateDesktopShortcut

Function CreateDesktopShortcut
  CreateShortCut "$DESKTOP\CedarLogic.lnk" "$INSTDIR\CedarLogic.exe"
FunctionEnd

!endif
