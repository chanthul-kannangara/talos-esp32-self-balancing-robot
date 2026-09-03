param(
  [string]$PortName = "COM9",
  [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"
$databaseUrl = "https://esp32-self-balancing-robot-default-rtdb.asia-southeast1.firebasedatabase.app"
$statusUrl = "$databaseUrl/robot/status.json"
$serialPort = [System.IO.Ports.SerialPort]::new($PortName, $BaudRate, "None", 8, "One")
$serialPort.NewLine = "`n"
$serialPort.ReadTimeout = 1500
$serialPort.Open()
$writerInfo = [System.Diagnostics.ProcessStartInfo]::new()
$writerInfo.FileName = "C:\Program Files\nodejs\node.exe"
$writerInfo.Arguments = "`"$PSScriptRoot\firebase-status-writer.mjs`""
$writerInfo.UseShellExecute = $false
$writerInfo.RedirectStandardInput = $true
$writerInfo.CreateNoWindow = $true
$firebaseWriter = [System.Diagnostics.Process]::Start($writerInfo)

$status = @{
  online = $true
  state = "disarmed"
  balancingEnabled = $false
  standingEnabled = $false
  tilt = 0.0
  gyroY = 0.0
  pidOutput = 0.0
  wheelLeftUs = 1500
  wheelRightUs = 1500
  legLeftUs = 1500
  legRightUs = 1500
  heightCm = 0.0
  imuErrors = 0
  encoderErrors = 0
  encodersHealthy = $false
  wifiRssi = 0
  loopTimeUs = 2000
  transport = "usb-com9"
  updatedAt = @{ ".sv" = "timestamp" }
}

Write-Host "TALOS serial bridge listening on $PortName at $BaudRate baud."

try {
  while ($true) {
    try {
      $line = $serialPort.ReadLine().Trim()
    } catch [System.TimeoutException] {
      continue
    }

    if ($line -match '^state=(\w+) stand=(\w+) angle=([-+\d.]+) gyro=([-+\d.]+) pid=([-+\d.]+) height=([-+\d.]+)cm.*pulse=(\d+)/(\d+) imuerr=(\d+) encerr=(\d+)$') {
      $status.state = $Matches[1]
      $status.balancingEnabled = $Matches[1] -eq "armed"
      $status.standingEnabled = $Matches[2] -eq "on"
      $status.tilt = [double]$Matches[3]
      $status.gyroY = [double]$Matches[4]
      $status.pidOutput = [double]$Matches[5]
      $status.heightCm = [double]$Matches[6]
      $status.wheelLeftUs = [int]$Matches[7]
      $status.wheelRightUs = [int]$Matches[8]
      $status.imuErrors = [int]$Matches[9]
      $status.encoderErrors = [int]$Matches[10]
      continue
    }

    if ($line -match '^encL=.*magnet=(\d+) pulse=(\d+).*encR=.*magnet=(\d+) pulse=(\d+)') {
      $status.encodersHealthy = $Matches[1] -eq "1" -and $Matches[3] -eq "1"
      $status.legLeftUs = [int]$Matches[2]
      $status.legRightUs = [int]$Matches[4]
      $payload = $status | ConvertTo-Json -Compress
      $firebaseWriter.StandardInput.WriteLine($payload)
      $firebaseWriter.StandardInput.Flush()
      Write-Host ("{0:T}  {1}, {2:N2} deg, {3:N2} cm" -f (Get-Date), $status.state, $status.tilt, $status.heightCm)
    }
  }
} finally {
  if ($serialPort.IsOpen) {
    $serialPort.Close()
  }
  if (!$firebaseWriter.HasExited) {
    $firebaseWriter.StandardInput.Close()
    $firebaseWriter.WaitForExit(2000) | Out-Null
  }
}
