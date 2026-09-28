/*
_____________________________________________________________________________

                     Close the old CedarLogic, start the new one
_____________________________________________________________________________

 An automatic update runs this installer silently while CedarLogic is still on
 screen, and until now nothing closed the old copy or started the replacement:
 the update landed on disk and the user was left looking at the version they
 had before, with no sign anything had happened.

 The program now closes itself when the updater asks (see WinSparkleUpdater),
 but closing takes as long as it takes -- there may be a "save your circuit?"
 prompt in the way -- so the installer waits for the old copy to let go of its
 own executable before writing over it, and starts the new one when it is done.

 Usage:
   !include "RestartAfterUpdate.nsh"
   ${WaitForCedarLogicToExit}   ; before the files are written
   ${RestartCedarLogic}         ; after they are written

 Both are safe when CedarLogic was not running, which is the case for someone
 installing by hand.
_____________________________________________________________________________
*/

!ifndef RESTART_AFTER_UPDATE_NSH
!define RESTART_AFTER_UPDATE_NSH

!macro WaitForCedarLogicToExit
  Push $R0

  cedarlogic_wait_loop:
    ; A loaded Windows executable may still be marked for deletion, so Delete
    ; does not prove the old program has stopped. tasklist + findstr returns 0
    ; only while a CedarLogic process exists; wait for that process, then write.
    nsExec::Exec '"$SYSDIR\cmd.exe" /C tasklist /FI "$\"IMAGENAME eq CedarLogic.exe$\"" /NH | findstr /I /C:"$\"CedarLogic.exe$\"" >NUL'
    Pop $R0
    StrCmp $R0 0 0 cedarlogic_wait_done
    DetailPrint "Waiting for CedarLogic to close..."
    Sleep 500
    Goto cedarlogic_wait_loop

  cedarlogic_wait_done:
  Pop $R0
!macroend

!macro RestartCedarLogic
  ; Only for an update the user did not ask to watch. A hand-run installer ends
  ; on its own page, where starting the program unasked would be a surprise.
  IfSilent 0 cedarlogic_restart_done
  IfFileExists "$INSTDIR\CedarLogic.exe" 0 cedarlogic_restart_done

  ; Through Explorer rather than directly. This installer asked for
  ; administrator rights, and anything it starts inherits them: an elevated
  ; CedarLogic cannot be dropped a .cdl file from a normal Explorer window,
  ; because Windows blocks drag and drop from a lower privilege level to a
  ; higher one. Explorer is running as the user, so what it starts is too.
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\CedarLogic.exe"'

  cedarlogic_restart_done:
!macroend

!define WaitForCedarLogicToExit "!insertmacro WaitForCedarLogicToExit"
!define RestartCedarLogic "!insertmacro RestartCedarLogic"

!endif
