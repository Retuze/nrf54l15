#Requires -Version 5.1
<#
.SYNOPSIS
  启动 OpenOCD RTT 服务，经 TCP 9090 采集日志并保存到文件。

.DESCRIPTION
  1. 可选释放 9090 端口
  2. 后台启动: openocd ... init; reset run; nrf54l-rtt
  3. 等待 9090 就绪后连接并写入 logs/rtt_YYYYMMDD_HHMMSS.log
  4. 到时或 Ctrl+C 后断开并结束 OpenOCD

.EXAMPLE
  .\scripts\rtt_capture.ps1
  .\scripts\rtt_capture.ps1 -DurationSec 180
  .\scripts\rtt_capture.ps1 -NoKillOpenOcd    # OpenOCD 已在跑（如 VS Code openocd-rtt）
  .\scripts\rtt_capture.ps1 -SkipOpenOcd      # 仅连 9090 抓日志
#>
param(
    [int]  $DurationSec   = 120,
    [string]$OutDir        = "",
    [switch]$NoKillPort,
    [switch]$SkipOpenOcd,
    [switch]$FlashFirst
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot "logs"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$ts      = Get-Date -Format "yyyyMMdd_HHmmss"
$LogFile = Join-Path $OutDir "rtt_$ts.log"
$OcdOut  = Join-Path $OutDir "openocd_rtt_$ts.stdout.log"
$OcdErr  = Join-Path $OutDir "openocd_rtt_$ts.stderr.log"

function Test-TcpPort {
    param([int]$Port)
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
        $ok  = $iar.AsyncWaitHandle.WaitOne(500)
        if (-not $ok) { return $false }
        $client.EndConnect($iar)
        return $true
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Stop-PortListeners {
    param([int]$Port)
    if (-not (Get-Command Get-NetTCPConnection -ErrorAction SilentlyContinue)) {
        return
    }
    Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
        ForEach-Object {
            $owningPid = $_.OwningProcess
            if ($owningPid -and $owningPid -ne 0) {
                Write-Host "[rtt] stop PID $owningPid (port $Port)"
                Stop-Process -Id $owningPid -Force -ErrorAction SilentlyContinue
            }
        }
    Start-Sleep -Milliseconds 300
}

$openocdProc = $null

try {
    if ($FlashFirst) {
        Write-Host "[rtt] flash firmware..."
        & openocd -f boards/xiao_nrf54l15/support/openocd.cfg `
            -c "init;reset halt" `
            -c "nrf54l-load build/debug/firmware.elf" `
            -c "nrf54l-verify build/debug/firmware.elf" `
            -c "reset run; shutdown"
        if ($LASTEXITCODE -ne 0) {
            Write-Error "openocd flash failed (exit $LASTEXITCODE). Probe connected?"
        }
    }

    if (-not $SkipOpenOcd) {
        if (-not $NoKillPort) {
            Stop-PortListeners -Port 9090
            Stop-PortListeners -Port 4444
        }

        Write-Host "[rtt] starting OpenOCD (RTT server on 9090)..."
        $ocdCmd = "openocd -f boards/xiao_nrf54l15/support/openocd.cfg " `
            + "-c `"init;reset run`" -c nrf54l-rtt 1>`"$OcdOut`" 2>`"$OcdErr`""
        $openocdProc = Start-Process -FilePath "cmd.exe" `
            -ArgumentList @("/c", $ocdCmd) `
            -PassThru `
            -WindowStyle Hidden

        $deadline = (Get-Date).AddSeconds(45)
        while (-not (Test-TcpPort -Port 9090)) {
            if ($openocdProc.HasExited) {
                Get-Content $OcdErr -ErrorAction SilentlyContinue | Write-Host
                throw "OpenOCD exited before RTT ready (code $($openocdProc.ExitCode)). See $OcdErr"
            }
            if ((Get-Date) -gt $deadline) {
                throw "Timeout waiting for RTT port 9090. See $OcdErr"
            }
            Start-Sleep -Milliseconds 200
        }
        Write-Host "[rtt] port 9090 ready"
    } else {
        if (-not (Test-TcpPort -Port 9090)) {
            throw "SkipOpenOcd set but 9090 not listening. Start VS Code task openocd-rtt first."
        }
    }

    Write-Host "[rtt] capture -> $LogFile ($DurationSec s, Ctrl+C to stop)"
    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect("127.0.0.1", 9090)
    $stream = $client.GetStream()
    $fs     = [System.IO.File]::Open($LogFile, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $buf    = New-Object byte[] 4096
    $enc    = [System.Text.Encoding]::UTF8
    $endAt  = (Get-Date).AddSeconds($DurationSec)

    while ((Get-Date) -lt $endAt) {
        if ($stream.DataAvailable) {
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -gt 0) {
                $fs.Write($buf, 0, $n)
                $fs.Flush()
                [Console]::Write($enc.GetString($buf, 0, $n))
            }
        } else {
            Start-Sleep -Milliseconds 10
        }
    }
} finally {
    if ($null -ne $fs) { $fs.Close() }
    if ($null -ne $client) { $client.Close() }
    if ($null -ne $openocdProc -and -not $openocdProc.HasExited) {
        Write-Host "[rtt] stopping OpenOCD (PID $($openocdProc.Id))"
        Stop-Process -Id $openocdProc.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ""
Write-Host "[rtt] saved: $LogFile"
if (Test-Path $OcdErr) {
    Write-Host "[rtt] openocd stderr: $OcdErr"
}
