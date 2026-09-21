# Deploying CedarLogic

Notes for administrators rolling CedarLogic out across managed machines, rather
than letting each person install and update their own copy.

## Turning off update checks (Windows)

By default CedarLogic checks for a newer version in the background, and offers
one after a crash. In a managed deployment the organisation decides which
version is installed, so both of those are unwanted. Set this policy and the
program never contacts the update server at all:

```
Key:   HKEY_LOCAL_MACHINE\SOFTWARE\Policies\Cedarville University\CedarLogic
Value: DisableUpdateChecks
Type:  REG_DWORD
Data:  1
```

Any non-zero value turns checking off. Remove the value, or set it to 0, and
normal update checking returns.

With the policy set:

- no update check runs at startup, and no background updater thread starts
- the crash reporter stops offering updates and only offers to report the
  problem, so it makes no network request either
- **Help ▸ Check for Updates...** is greyed out

The program still reports crashes if a person chooses to, which is a separate
action they take deliberately. Nothing else reaches the network.

### Why this key

`SOFTWARE\Policies` is the standard location for administrative policy on
Windows. Only administrators can write there, so someone using the machine
cannot switch update checking back on, and it is the tree that both Group Policy
and Intune already target.

### Setting it with Intune

Deploy it as a **Settings catalog** or **Custom (OMA-URI)** profile, or with a
device-context PowerShell script:

```powershell
$key = 'HKLM:\SOFTWARE\Policies\Cedarville University\CedarLogic'
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name 'DisableUpdateChecks' `
                 -PropertyType DWord -Value 1 -Force | Out-Null
```

Run it in the **system** context, not the user context: the value lives under
HKLM, which a standard user account cannot write.

### A note about 32-bit

CedarLogic ships as a 32-bit program. Normally a 32-bit program reading
`HKLM\SOFTWARE\...` is quietly redirected to `HKLM\SOFTWARE\WOW6432Node\...`,
which is *not* where an administrator writes the policy. CedarLogic reads the
ordinary 64-bit location first and only then the redirected one, so write the
policy to the plain path above and it will be found. Both work.

### Checking it took effect

```powershell
Get-ItemProperty -Path 'HKLM:\SOFTWARE\Policies\Cedarville University\CedarLogic' `
                 -Name DisableUpdateChecks
```

Then start CedarLogic and open the Help menu: **Check for Updates...** is greyed
out when the policy is active.

## Every registry key CedarLogic touches (Windows)

The policy above is the only key CedarLogic reads to decide how to behave. The
rest are bookkeeping written by the installer and by the updater. They are
listed here so a deployment can detect an existing install, clean up after one,
or account for what a packaged copy leaves behind.

### Written by the installer, under HKLM

The installer runs as administrator and installs for all users, so both of these
are machine-wide. The uninstaller removes them.

```
HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\CedarLogic <version>
    DisplayName, DisplayVersion, DisplayIcon, Publisher, UninstallString,
    HelpLink, URLInfoAbout, NoModify, NoRepair        (all REG_SZ or REG_DWORD)

HKLM\SOFTWARE\Cedarville University\CedarLogic <version>
    (default)   REG_SZ   the install directory
```

Note the version in both key names. It comes from the packaging and changes
every release, so a detection rule that hardcodes `CedarLogic 3.1.2` stops
matching the moment a new version ships. Enumerate the subkeys and match on the
`CedarLogic ` prefix instead, or key off `DisplayName` and `Publisher`.

### The .cdl file association, under HKCR

```
HKCR\.cdl
    (default)    REG_SZ   CedarLogic Project
    backup_val   REG_SZ   whatever .cdl pointed at before, if anything

HKCR\CedarLogic Project
    (default)                  REG_SZ   CedarLogic Project
    shell\(default)            REG_SZ   open
    DefaultIcon\(default)      REG_SZ   <install dir>\CedarLogic.exe,0
    shell\open\command\...     REG_SZ   "<install dir>\CedarLogic.exe" "%1"
    shell\edit\command\...     REG_SZ   "<install dir>\CedarLogic.exe" "%1"
```

Uninstalling restores `backup_val` to the `.cdl` default and deletes the
`CedarLogic Project` key. If you deploy by unpacking files rather than running
the installer, none of this is written and double-clicking a `.cdl` file will
not open CedarLogic.

### Written by the updater, under HKCU

WinSparkle keeps its own state per user. Nothing here needs to be deployed; it
is listed because it is what a user profile accumulates, and because its
`CheckForUpdates` value is easy to mistake for a way to enforce policy.

```
HKCU\Software\Cedarville University\CedarLogic\WinSparkle
    CheckForUpdates    REG_SZ   "1" or "0"; absent means "not asked yet"
    LastCheckTime      REG_SZ   Unix timestamp of the last appcast fetch
    UpdateInterval     REG_SZ   seconds between checks, default 86400
    SkipThisVersion    REG_SZ   version the user pressed Skip on
    DidRunOnce         REG_SZ   "1" after the first launch
    UpdateTempDir      REG_SZ   in-flight download, cleaned up next launch
```

Every value is a string, including the ones that look like numbers and flags.
WinSparkle refuses to read a `REG_DWORD` written in their place and treats it as
missing.

**`CheckForUpdates` is not a policy.** WinSparkle reads each value from HKCU
first and only falls back to HKLM, so a machine-wide value is a default that the
user's own setting overrides the moment they touch the checkbox. That asymmetry
is why `DisableUpdateChecks` exists as a separate key: CedarLogic checks it
before WinSparkle starts at all, so there is no background thread and no HKCU
value that can undo it.

### Where these actually land on 64-bit Windows

CedarLogic and its installer are both 32-bit, and neither asks for the 64-bit
registry view, so Windows redirects their `SOFTWARE` paths:

| Written path | Real location |
| --- | --- |
| `HKLM\SOFTWARE\Microsoft\...\Uninstall\...` | `HKLM\SOFTWARE\WOW6432Node\Microsoft\...\Uninstall\...` |
| `HKLM\SOFTWARE\Cedarville University\...` | `HKLM\SOFTWARE\WOW6432Node\Cedarville University\...` |
| `HKCU\Software\Cedarville University\...` | `HKCU\Software\WOW6432Node\Cedarville University\...` |

Add and Remove Programs shows both views, so the uninstall entry appears
normally. A script does not get that for free: PowerShell run as 64-bit reads
the 64-bit view and will find none of these. Read the redirected paths, or run
the script 32-bit.

The `DisableUpdateChecks` policy is the exception. CedarLogic deliberately reads
the plain 64-bit path first, so write it where the section above says and leave
redirection out of it.

## macOS

There is no equivalent policy on macOS. Updates there go through Sparkle, which
is configured with a standard configuration profile; see the Sparkle
documentation for the `SUEnableAutomaticChecks` key. Updates there go through Sparkle, which
is configured with a standard configuration profile; see the Sparkle
documentation for the `SUEnableAutomaticChecks` key.
