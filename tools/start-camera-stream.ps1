param(
  [string]$CameraHost = "talos-camera.local",
  [int]$RelayPort = 8787
)

$ErrorActionPreference = "Stop"
$CameraHost = $CameraHost.Trim()
if ($CameraHost -match "^https?://") {
  $CameraHost = ([Uri]$CameraHost).Host
}
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolDir = Join-Path $env:LOCALAPPDATA "TalosTools"
$cloudflared = Join-Path $toolDir "cloudflared.exe"
$tokenFile = Join-Path $toolDir "relay-token.txt"
$node = (Get-Command node.exe -ErrorAction Stop).Source
$firebase = (Get-Command firebase.cmd -ErrorAction Stop).Source
$relay = Join-Path $PSScriptRoot "camera-relay.mjs"

if (-not (Test-Path $cloudflared)) {
  throw "cloudflared.exe is missing from $toolDir"
}

try {
  $cameraResponse = Invoke-WebRequest -UseBasicParsing -TimeoutSec 4 "http://$CameraHost"
  if ($cameraResponse.StatusCode -ne 200) { throw "HTTP $($cameraResponse.StatusCode)" }
} catch {
  throw "ESP32-CAM is not reachable at $CameraHost. Confirm the laptop and camera are on the same Wi-Fi, then enter the IP shown in Serial Monitor."
}

if (Test-Path $tokenFile) {
  $token = (Get-Content $tokenFile -Raw).Trim()
} else {
  $bytes = New-Object byte[] 32
  $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
  $rng.GetBytes($bytes)
  $rng.Dispose()
  $token = ([BitConverter]::ToString($bytes)).Replace("-", "").ToLowerInvariant()
  [IO.File]::WriteAllText($tokenFile, $token, (New-Object Text.UTF8Encoding($false)))
}

Get-CimInstance Win32_Process |
  Where-Object {
    ($_.Name -eq "node.exe" -and $_.CommandLine -like "*camera-relay.mjs*") -or
    $_.Name -eq "cloudflared.exe"
  } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force }

$env:PORT = [string]$RelayPort
$env:CAMERA_RELAY_TOKEN = $token
$env:CAMERA_STREAM_URL = "http://${CameraHost}:81/stream"
$env:CAMERA_CAPTURE_URL = "http://${CameraHost}/capture"
$env:TALOS_ALLOWED_ORIGIN = "https://talosone.web.app"

$relayOut = Join-Path $toolDir "camera-relay.out.log"
$relayErr = Join-Path $toolDir "camera-relay.err.log"
Remove-Item $relayOut, $relayErr -Force -ErrorAction SilentlyContinue
Start-Process -FilePath $node -ArgumentList "`"$relay`"" -WorkingDirectory $projectRoot -WindowStyle Hidden -RedirectStandardOutput $relayOut -RedirectStandardError $relayErr

$relayReady = $false
for ($attempt = 0; $attempt -lt 20; $attempt += 1) {
  Start-Sleep -Milliseconds 500
  try {
    Invoke-RestMethod -TimeoutSec 2 "http://127.0.0.1:${RelayPort}/health" | Out-Null
    $relayReady = $true
    break
  } catch {}
}

if (-not $relayReady) {
  throw "Camera relay failed: $(Get-Content $relayErr -Raw -ErrorAction SilentlyContinue)"
}

$tunnelOut = Join-Path $toolDir "cloudflared-talos.out.log"
$tunnelErr = Join-Path $toolDir "cloudflared-talos.err.log"
Remove-Item $tunnelOut, $tunnelErr -Force -ErrorAction SilentlyContinue
Start-Process -FilePath $cloudflared -ArgumentList @("tunnel", "--protocol", "http2", "--url", "http://127.0.0.1:${RelayPort}") -WindowStyle Hidden -RedirectStandardOutput $tunnelOut -RedirectStandardError $tunnelErr

$tunnelUrl = $null
$connected = $false
for ($attempt = 0; $attempt -lt 60; $attempt += 1) {
  Start-Sleep -Seconds 1
  $log = (Get-Content $tunnelOut -Raw -ErrorAction SilentlyContinue) + "`n" + (Get-Content $tunnelErr -Raw -ErrorAction SilentlyContinue)
  $match = [regex]::Match($log, "https://[a-z0-9-]+\.trycloudflare\.com")
  if ($match.Success) { $tunnelUrl = $match.Value }
  if ($log -match "Registered tunnel connection") {
    $connected = $true
    break
  }
}

if (-not $tunnelUrl -or -not $connected) {
  throw "Secure tunnel failed to connect. See $tunnelErr"
}

$streamUrl = "https://$(([Uri]$tunnelUrl).Host)/stream?token=$token"
$jsonFile = Join-Path $env:TEMP "talos-camera-url.json"
[IO.File]::WriteAllText($jsonFile, ($streamUrl | ConvertTo-Json -Compress), (New-Object Text.UTF8Encoding($false)))

& $firebase database:set /robot/camera/streamUrl $jsonFile --force --project esp32-self-balancing-robot --instance esp32-self-balancing-robot-default-rtdb | Out-Null
& $firebase database:set /robot/camera/online --data true --force --project esp32-self-balancing-robot --instance esp32-self-balancing-robot-default-rtdb | Out-Null
Remove-Item $jsonFile -Force

Write-Host "TALOS camera stream is live inside https://talosone.web.app" -ForegroundColor Green
Write-Host "Keep this laptop awake and connected to the camera network."
