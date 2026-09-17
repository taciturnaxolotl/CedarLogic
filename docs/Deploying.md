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

## macOS

There is no equivalent policy on macOS. Updates there go through Sparkle, which
is configured with a standard configuration profile; see the Sparkle
documentation for the `SUEnableAutomaticChecks` key.
