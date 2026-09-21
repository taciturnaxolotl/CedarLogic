/*
_____________________________________________________________________________

                     Remove legacy versioned installations
_____________________________________________________________________________

 Releases up to and including 3.2.0 installed as "CedarLogic <version>". CPack
 derives the install directory, the Start Menu folder and the Add/Remove
 Programs registry key from that one name, so every release was a stranger to
 the one before it: it installed into its own directory, and the "uninstall the
 previous version first" step looked for its own version-stamped key and of
 course never found the older one. Machines collected a copy per release.

 The name is now plain "CedarLogic", which fixes it from here on. This macro
 clears up what the old scheme already left behind, by uninstalling anything
 still registered as "CedarLogic <something>".

 Usage:
   !include "RemoveLegacyVersions.nsh"
   ${RemoveLegacyVersions}

 Safe to run when there is nothing to remove, which is the normal case.

 Note on the registry paths: the installer is 32-bit, so Windows redirects
 these reads to SOFTWARE\WOW6432Node automatically. That is also where the old
 32-bit installers wrote them, so the two line up without asking for a
 particular registry view.
_____________________________________________________________________________
*/

!ifndef REMOVE_LEGACY_VERSIONS_NSH
!define REMOVE_LEGACY_VERSIONS_NSH

; Must match CPACK_PACKAGE_VENDOR in CMakeLists.txt. CPack records each
; install's directory under this key, which saves parsing it back out of the
; quoted UninstallString.
!define LEGACY_VENDOR "Cedarville University"
!define LEGACY_UNINST_ROOT "Software\Microsoft\Windows\CurrentVersion\Uninstall"

!macro RemoveLegacyVersions
  Push $R0  ; enumeration index
  Push $R1  ; subkey name, e.g. "CedarLogic 3.1.2"
  Push $R2  ; name prefix under test
  Push $R3  ; UninstallString
  Push $R4  ; install directory

  StrCpy $R0 0

  legacy_next:
    EnumRegKey $R1 HKLM "${LEGACY_UNINST_ROOT}" $R0
    StrCmp $R1 "" legacy_done

    ; "CedarLogic " with the trailing space, so this matches only the old
    ; version-stamped names and never the plain "CedarLogic" being installed.
    StrCpy $R2 $R1 11
    StrCmp $R2 "CedarLogic " 0 legacy_skip

    ReadRegStr $R3 HKLM "${LEGACY_UNINST_ROOT}\$R1" "UninstallString"
    StrCmp $R3 "" legacy_skip

    ReadRegStr $R4 HKLM "Software\${LEGACY_VENDOR}\$R1" ""
    StrCmp $R4 "" legacy_skip

    DetailPrint "Removing previous installation: $R1"
    ; _?= runs the uninstaller in place rather than from a temp copy, which is
    ; what makes ExecWait actually wait for it to finish. It also means the
    ; uninstaller cannot delete itself, hence the tidy-up below.
    ExecWait '$R3 /S _?=$R4'
    Delete "$R4\Uninstall.exe"
    ; Plain RMDir, not RMDir /r: if anything unexpected is left in there, leave
    ; a stray folder behind rather than delete a tree we did not verify.
    RMDir "$R4"

    ; If the uninstaller removed the key, every later key has shifted down into
    ; this index, so read the same index again. If the key survived, the
    ; uninstall did not happen and stepping over it is what stops this looping
    ; forever.
    ReadRegStr $R3 HKLM "${LEGACY_UNINST_ROOT}\$R1" "UninstallString"
    StrCmp $R3 "" legacy_next

  legacy_skip:
    IntOp $R0 $R0 + 1
    Goto legacy_next

  legacy_done:
  Pop $R4
  Pop $R3
  Pop $R2
  Pop $R1
  Pop $R0
!macroend

!define RemoveLegacyVersions "!insertmacro RemoveLegacyVersions"

!endif
