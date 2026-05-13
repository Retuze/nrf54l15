$ErrorActionPreference = 'Stop'

Write-Host 'Starting OpenOCD RTT server...' -ForegroundColor Green

$job = Start-Job -ScriptBlock {
    param($wd)
    Set-Location $wd
    & openocd -f boards/xiao_nrf54l15/support/openocd.cfg -c init -c 'reset run' -c 'rtt setup 0x20000000 0x40000' -c 'rtt start' -c 'rtt server start 9990 0' 2>&1
} -ArgumentList $PWD.Path

# Wait for RTT server port
Write-Host 'Waiting for RTT server on port 9990...' -ForegroundColor Yellow
$timeout = 30
$ready = $false
for ($i = 0; $i -lt $timeout; $i++) {
    if ($job.State -eq 'Failed') {
        Write-Host 'OpenOCD exited unexpectedly:' -ForegroundColor Red
        Receive-Job $job | Write-Host
        Remove-Job $job -Force
        exit 1
    }
    try {
        $test = New-Object System.Net.Sockets.TcpClient('localhost', 9990)
        $test.Close()
        $ready = $true
        break
    } catch {
        Start-Sleep -Seconds 1
    }
}

if (-not $ready) {
    Write-Host "Timeout: RTT server didn't start within ${timeout}s" -ForegroundColor Red
    Stop-Job $job
    Receive-Job $job | Write-Host
    Remove-Job $job -Force
    exit 1
}

Write-Host 'Connected. Ctrl+C to disconnect.' -ForegroundColor Green

# Read RTT output
$tcp = New-Object System.Net.Sockets.TcpClient('localhost', 9990)
$stream = $tcp.GetStream()
$reader = New-Object System.IO.StreamReader($stream)

try {
    while ($true) {
        $line = $reader.ReadLine()
        if ($line -ne $null) { Write-Host $line }
    }
} catch {
    Write-Host "`nDisconnected." -ForegroundColor Yellow
} finally {
    $reader.Dispose()
    $stream.Dispose()
    $tcp.Dispose()
    Stop-Job $job -ErrorAction SilentlyContinue
    Remove-Job $job -Force -ErrorAction SilentlyContinue
    # Make sure openocd is killed
    Get-Process openocd -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host 'OpenOCD stopped.' -ForegroundColor Yellow
}
