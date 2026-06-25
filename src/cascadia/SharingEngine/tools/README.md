# pane-sharing test/setup helpers

Small PowerShell helpers for verifying and setting up read-only pane sharing
across two machines (host = PC-A, viewer = PC-B).

## `Check-PaneShare.ps1` — viewer-side connectivity checker (PC-B)

Connects to a share URL, authenticates with its token, and reports whether the
host sends a SNAPSHOT — **without launching Windows Terminal**. Run it on the
viewer machine to isolate network / firewall / token problems before trying the
full app. Works in Windows PowerShell 5.1 and PowerShell 7 (Windows/macOS/Linux).

```powershell
.\Check-PaneShare.ps1 -Url "ws://192.168.1.50:51234/?token=abc123"
# or
.\Check-PaneShare.ps1 -HostName 192.168.1.50 -Port 51234 -Token abc123
```

Outcomes: `TCP reach FAILED` = wrong IP / not sharing / firewall; `rejected ->
unauthorized|expired` = bad/old token; `SNAPSHOT received` = reachable + token OK.

## `Enable-OpenSSHServer.ps1` — host-side SSH setup (PC-A)

Enables the Windows OpenSSH **Server**, starts it, opens TCP 22 in the firewall,
and (optionally) authorizes a viewer's public key in the correct file — handling
the admin-account `administrators_authorized_keys` + ACL gotcha automatically.
Run elevated (Administrator).

```powershell
# Enable the server only:
.\Enable-OpenSSHServer.ps1

# Enable + authorize the viewer's key:
.\Enable-OpenSSHServer.ps1 -PublicKeyPath C:\path\to\id_ed25519.pub
```

This is the host-side prerequisite for the recommended secure remote path,
`connectSharedSessionViaSsh` (see the pane-sharing section in the repo README).
