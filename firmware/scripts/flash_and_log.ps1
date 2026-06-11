param(
    [string]$Port = "COM3",
    [string]$BuildDir = "build_try2",
    [int]$BaudRate = 115200,
    [int]$MonitorDelaySeconds = 2
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$firmwareDir = Split-Path -Parent $scriptDir
$logDir = Join-Path $firmwareDir "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logPath = Join-Path $logDir "startup_${Port}_${stamp}.log"

Write-Host "Flashing $BuildDir to $Port"
$exportScript = Join-Path $firmwareDir "..\.esp-idf\export.ps1"
$idfCommand = ". `"$exportScript`"; idf.py -B `"$BuildDir`" -p $Port flash"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $idfCommand

Write-Host "Waiting $MonitorDelaySeconds seconds before monitor..."
Start-Sleep -Seconds $MonitorDelaySeconds

Write-Host "Writing startup log to $logPath"
$portHandle = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, "None", 8, "One"
$portHandle.ReadTimeout = 500
$portHandle.NewLine = "`n"
$portHandle.DtrEnable = $true
$portHandle.RtsEnable = $true
$portHandle.Open()

try {
    Add-Content -Path $logPath -Value ("[{0}] monitor started on {1} @ {2}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $Port, $BaudRate)
    while ($true) {
        try {
            $line = $portHandle.ReadLine()
            $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
            $text = "[{0}] {1}" -f $timestamp, $line.TrimEnd("`r", "`n")
            Write-Host $text
            Add-Content -Path $logPath -Value $text
        } catch [System.TimeoutException] {
            continue
        }
    }
} finally {
    if ($portHandle.IsOpen) {
        $portHandle.Close()
    }
}
