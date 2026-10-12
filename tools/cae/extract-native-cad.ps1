param(
    [string]$CadRoot = (Join-Path $PSScriptRoot '..\..\cad\PARTS'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\outputs\local-cad-extraction')
)
$ErrorActionPreference = 'Stop'
$null = New-Item -ItemType Directory -Path $OutputRoot -Force
$before = Get-ChildItem -LiteralPath $CadRoot -File | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash
$before | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputRoot 'cad-source-sha256-before.json') -Encoding utf8
$interop = 'C:\Program Files\SOLIDWORKS Corp\SOLIDWORKS\api\redist\SolidWorks.Interop.sldworks.dll'
Add-Type -Path $interop
Add-Type -Path (Join-Path $PSScriptRoot 'ReadTalosCad.cs') -ReferencedAssemblies @($interop, 'System.Collections.dll', 'System.Runtime.dll', 'System.Runtime.InteropServices.dll')
try {
    $result = [ReadTalosCad]::Read($CadRoot, $OutputRoot)
    $result | ConvertTo-Json -Depth 35 | Set-Content -LiteralPath (Join-Path $OutputRoot 'native-cad.json') -Encoding utf8
    'Read-only CAD extraction complete.'
} finally {
    $after = Get-ChildItem -LiteralPath $CadRoot -File | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash
    $after | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputRoot 'cad-source-sha256-after.json') -Encoding utf8
    $comparison = Compare-Object $before $after -Property Path,Hash
    if ($comparison) { throw 'CAD source hashes changed.' }
    'All original CAD hashes unchanged.'
}
