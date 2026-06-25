<#
.SYNOPSIS
    Standalone connectivity checker for the read-only pane-sharing feature.

.DESCRIPTION
    Connects to a share URL, authenticates with its token, and reports whether the
    host sends a SNAPSHOT -- WITHOUT launching Windows Terminal. Use it on the
    viewer machine (PC-B) to isolate network / firewall / token problems before
    trying the full app:

        TCP reach FAILED         -> wrong IP, host down, or firewall blocking
        WebSocket connect FAILED -> something is listening but not the share server
        rejected -> unauthorized -> wrong token
        rejected -> expired      -> token lifetime elapsed (re-share)
        SNAPSHOT received        -> reachable + token OK; the app will work

    Works in Windows PowerShell 5.1 and PowerShell 7 (Windows/macOS/Linux).

.PARAMETER Url
    The full share URL, e.g. ws://192.168.1.50:51234/?token=abc123...

.PARAMETER HostName
    Host/IP (alternative to -Url). NB: -Host is reserved in PowerShell.

.PARAMETER Port
    Port (with -HostName).

.PARAMETER Token
    Share token (with -HostName).

.PARAMETER TimeoutSec
    Per-step timeout. Default 6.

.EXAMPLE
    .\Check-PaneShare.ps1 -Url "ws://192.168.1.50:51234/?token=abc123"

.EXAMPLE
    .\Check-PaneShare.ps1 -HostName 192.168.1.50 -Port 51234 -Token abc123
#>
[CmdletBinding(DefaultParameterSetName = 'Url')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Url', Position = 0)]
    [string]$Url,
    [Parameter(Mandatory, ParameterSetName = 'Parts')]
    [string]$HostName,
    [Parameter(Mandatory, ParameterSetName = 'Parts')]
    [int]$Port,
    [Parameter(Mandatory, ParameterSetName = 'Parts')]
    [string]$Token,
    [int]$TimeoutSec = 6
)

$ErrorActionPreference = 'Stop'
$scheme = 'ws'

if ($PSCmdlet.ParameterSetName -eq 'Url') {
    if ($Url -notmatch '^(wss?)://([^:/]+):(\d+)(?:/[^?]*)?\?.*?token=([^&\s]+)') {
        Write-Host "Could not parse URL. Expected ws://host:port/?token=..." -ForegroundColor Red
        exit 2
    }
    $scheme = $Matches[1]; $HostName = $Matches[2]; $Port = [int]$Matches[3]; $Token = $Matches[4]
}

$short = if ($Token.Length -gt 6) { $Token.Substring(0, 6) + '...' } else { $Token }
Write-Host "Target: ${HostName}:${Port}  token=$short"

# 1) Raw TCP reachability -- separates "can't reach the host/port" from WS issues.
Write-Host -NoNewline "TCP reach...... "
try {
    $tcp = [System.Net.Sockets.TcpClient]::new()
    $iar = $tcp.BeginConnect($HostName, $Port, $null, $null)
    if (-not $iar.AsyncWaitHandle.WaitOne([TimeSpan]::FromSeconds($TimeoutSec))) { throw 'timeout' }
    $tcp.EndConnect($iar); $tcp.Close()
    Write-Host "OK" -ForegroundColor Green
}
catch {
    Write-Host "FAILED ($($_.Exception.Message))" -ForegroundColor Red
    Write-Host "  -> host unreachable or port blocked: wrong IP, not sharing, or a firewall." -ForegroundColor Yellow
    exit 1
}

# 2) WebSocket handshake.
$ws = [System.Net.WebSockets.ClientWebSocket]::new()
$cts = [System.Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds($TimeoutSec))
Write-Host -NoNewline "WebSocket..... "
try {
    $ws.ConnectAsync([Uri]"${scheme}://${HostName}:${Port}/?token=${Token}", $cts.Token).GetAwaiter().GetResult() | Out-Null
    Write-Host "OK" -ForegroundColor Green
}
catch {
    $m = if ($_.Exception.InnerException) { $_.Exception.InnerException.Message } else { $_.Exception.Message }
    Write-Host "FAILED ($m)" -ForegroundColor Red
    exit 1
}

# 3) Send HELLO and expect a SNAPSHOT (0x01); a STATE (0x04) means rejected.
$hello = [System.Collections.Generic.List[byte]]::new()
$hello.Add([byte]0x10)
$hello.AddRange([System.Text.Encoding]::UTF8.GetBytes('{"protocolVersion":1,"token":"' + $Token + '"}'))
$ws.SendAsync([ArraySegment[byte]]::new($hello.ToArray()), [System.Net.WebSockets.WebSocketMessageType]::Binary, $true, $cts.Token).GetAwaiter().GetResult() | Out-Null

$buf = [ArraySegment[byte]]::new((New-Object byte[] 65536))
try {
    $r = $ws.ReceiveAsync($buf, $cts.Token).GetAwaiter().GetResult()
}
catch {
    Write-Host "RESULT: no reply after HELLO -- the server likely rejected and closed (bad token?)." -ForegroundColor Red
    exit 1
}

$op = $buf.Array[0]
$code = 1
switch ($op) {
    0x01 {
        Write-Host "RESULT: SNAPSHOT received ($($r.Count) bytes). Reachable + token OK -- the app will work." -ForegroundColor Green
        $code = 0
    }
    0x04 {
        $payload = if ($r.Count -gt 1) { [System.Text.Encoding]::UTF8.GetString($buf.Array[1..($r.Count - 1)]) } else { '(empty)' }
        Write-Host "RESULT: rejected by server -> $payload (wrong or expired token?)." -ForegroundColor Red
    }
    default {
        Write-Host ("RESULT: unexpected first opcode 0x{0:x2} (server version mismatch?)." -f $op) -ForegroundColor Yellow
    }
}
try { $ws.Dispose() } catch {}
exit $code
