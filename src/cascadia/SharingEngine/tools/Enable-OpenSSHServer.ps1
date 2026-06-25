<#
.SYNOPSIS
    Enables the Windows OpenSSH Server (sshd) on this host and, optionally,
    authorizes a viewer's public key -- so the pane-sharing SSH tunnel
    (connectSharedSessionViaSsh) can reach this PC securely.

.DESCRIPTION
    Run this on the HOST machine (PC-A) in an elevated (Administrator) PowerShell.
    It:
      1. installs the OpenSSH.Server capability (if missing),
      2. starts sshd and sets it to start automatically,
      3. ensures an inbound firewall rule for TCP 22,
      4. (optional) appends a public key to the correct authorized_keys file.

    Windows gotcha handled here: if the account you SSH into is a member of the
    local Administrators group, sshd reads keys from
    %ProgramData%\ssh\administrators_authorized_keys (NOT the per-user file), and
    that file must be ACL'd to Administrators+SYSTEM only. This script picks the
    right file and fixes the ACL automatically.

.PARAMETER PublicKey
    A public key line to authorize, e.g. "ssh-ed25519 AAAA... user@pcb".

.PARAMETER PublicKeyPath
    Path to a .pub file to authorize (alternative to -PublicKey).

.EXAMPLE
    # Enable the server only (then add keys yourself):
    .\Enable-OpenSSHServer.ps1

.EXAMPLE
    # Enable + authorize the viewer's key:
    .\Enable-OpenSSHServer.ps1 -PublicKeyPath C:\Users\me\.ssh\id_ed25519.pub
#>
[CmdletBinding()]
param(
    [string]$PublicKey,
    [string]$PublicKeyPath
)

$ErrorActionPreference = 'Stop'

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
}
if (-not (Test-Admin)) {
    Write-Host "Please run this in an elevated (Administrator) PowerShell." -ForegroundColor Red
    exit 1
}

# 1) Install OpenSSH.Server if needed.
$cap = Get-WindowsCapability -Online -Name 'OpenSSH.Server*' | Select-Object -First 1
if ($cap -and $cap.State -ne 'Installed') {
    Write-Host "Installing $($cap.Name) ..."
    Add-WindowsCapability -Online -Name $cap.Name | Out-Null
}
else {
    Write-Host "OpenSSH.Server already installed."
}

# 2) Service: start + automatic.
Set-Service -Name sshd -StartupType Automatic
Start-Service sshd
Write-Host "sshd service: $((Get-Service sshd).Status)"

# 3) Firewall rule for inbound TCP 22.
if (-not (Get-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -ErrorAction SilentlyContinue)) {
    New-NetFirewallRule -Name 'OpenSSH-Server-In-TCP' -DisplayName 'OpenSSH Server (sshd)' `
        -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22 | Out-Null
    Write-Host "Created inbound firewall rule for TCP 22."
}
else {
    Write-Host "Inbound firewall rule for TCP 22 already present."
}

# 4) Optional public key registration.
if ($PublicKeyPath) {
    if (-not (Test-Path $PublicKeyPath)) { Write-Host "Public key file not found: $PublicKeyPath" -ForegroundColor Red; exit 1 }
    $PublicKey = (Get-Content -Raw $PublicKeyPath).Trim()
}

if ($PublicKey) {
    # Is the current account a member of the local Administrators group? (well-known
    # SID S-1-5-32-544). If so, sshd uses administrators_authorized_keys for it.
    $isAdminAccount = [bool]([Security.Principal.WindowsIdentity]::GetCurrent().Groups | Where-Object { $_.Value -eq 'S-1-5-32-544' })

    if ($isAdminAccount) {
        $akf = Join-Path $env:ProgramData 'ssh\administrators_authorized_keys'
    }
    else {
        $akf = Join-Path $env:USERPROFILE '.ssh\authorized_keys'
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $akf) | Out-Null
    $existing = if (Test-Path $akf) { Get-Content $akf } else { @() }
    if ($existing -contains $PublicKey) {
        Write-Host "Key already authorized in: $akf"
    }
    else {
        Add-Content -Path $akf -Value $PublicKey -Encoding ascii
        Write-Host "Authorized key in: $akf"
    }

    if ($isAdminAccount) {
        # sshd ignores administrators_authorized_keys unless it is owned/ACL'd to
        # Administrators + SYSTEM only.
        icacls $akf /inheritance:r | Out-Null
        icacls $akf /grant 'Administrators:F' 'SYSTEM:F' | Out-Null
        Write-Host "Fixed ACL on administrators_authorized_keys (Administrators + SYSTEM only)."
    }
}
else {
    Write-Host "No -PublicKey/-PublicKeyPath given; server enabled but no key added."
}

# 5) Summary + this host's LAN addresses (handy for the viewer's SSH target).
Write-Host ""
Write-Host "This host's IPv4 addresses:" -ForegroundColor Cyan
Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike '127.*' } |
    Select-Object IPAddress, InterfaceAlias |
    Format-Table -AutoSize

Write-Host "On the viewer (PC-B): test with  ssh <user>@<this-ip>  then use" -ForegroundColor Cyan
Write-Host "  connectSharedSessionViaSsh  with that target and the host's ws://localhost:PORT/?token=... URL." -ForegroundColor Cyan
