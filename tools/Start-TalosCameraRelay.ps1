param(
  [Parameter(Mandatory = $true)]
  [string]$CameraIp,

  [string]$RelayToken = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$cloudflared = 'C:\Program Files (x86)\cloudflared\cloudflared.exe'

if (-not (Test-Path -LiteralPath $cloudflared)) {
  throw "cloudflared was not found. Install Cloudflare Tunnel first."
}

$env:CAMERA_STREAM_URL = "http://${CameraIp}:81/stream"
if ($RelayToken) {
  $env:CAMERA_RELAY_TOKEN = $RelayToken
} else {
  Remove-Item Env:CAMERA_RELAY_TOKEN -ErrorAction SilentlyContinue
}

$relay = Start-Process `
  -FilePath 'node.exe' `
  -ArgumentList 'tools/camera-relay.mjs' `
  -WorkingDirectory $projectRoot `
  -WindowStyle Hidden `
  -PassThru

try {
  Start-Sleep -Seconds 1
  Invoke-RestMethod -Uri 'http://127.0.0.1:8787/health' | ConvertTo-Json -Compress
  Write-Host ''
  Write-Host 'Copy the HTTPS URL printed by Cloudflare, add /stream, then save it in TALOS ONE.'
  if ($RelayToken) {
    Write-Host 'Append ?token=YOUR_TOKEN to the saved dashboard URL.'
  }
  & $cloudflared tunnel --url 'http://127.0.0.1:8787'
} finally {
  if ($relay -and -not $relay.HasExited) {
    Stop-Process -Id $relay.Id -Force -ErrorAction SilentlyContinue
  }
}
