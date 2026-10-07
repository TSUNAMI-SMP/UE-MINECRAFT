param([string]$EngineRoot = "", [string]$Level = "/Game/UE")
$ErrorActionPreference = "Stop"
try {
    $project = Join-Path $PSScriptRoot "UEBridge.uproject"
    if (!(Test-Path -LiteralPath $project)) { throw "Place this launcher next to the UEBridge.uproject you use." }
    if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { throw "Save your level and close Unreal Editor before launching. No process was stopped." }
    if ($Level -notmatch '^/Game/[A-Za-z0-9_/-]+$' -or $Level.Contains('..')) { throw "Level must be a saved /Game/... level asset path." }
    $map = Join-Path $PSScriptRoot ("Content\" + $Level.Substring(6).Replace('/', '\') + ".umap")
    if (!(Test-Path -LiteralPath $map)) { throw "Saved level not found: $map . Pass -Level /Game/your_level if it has another name." }
    $descriptor = Get-Content -LiteralPath $project -Raw | ConvertFrom-Json
    $association = [string]$descriptor.EngineAssociation
    if (!$EngineRoot) {
        foreach ($key in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association", "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$association")) {
            if (Test-Path $key) { $candidate = (Get-ItemProperty -LiteralPath $key).InstalledDirectory
                if ($candidate -and (Test-Path (Join-Path $candidate "Engine\Binaries\Win64\UnrealEditor.exe"))) { $EngineRoot = $candidate; break } }
        }
    }
    if (!$EngineRoot) { $EngineRoot = (Read-Host "Enter the Unreal Engine installation folder (example C:\Program Files\Epic Games\UE_5.8)").Trim('"') }
    $editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    if (!(Test-Path -LiteralPath $editor)) { throw "UnrealEditor.exe was not found in this Unreal Engine installation." }
    Write-Host "Starting the saved level as a standalone game: 1920x1080, D3D11, no editor viewport." -ForegroundColor Green
    Write-Host "Minecraft: /uebridge video transport auto ; /uebridge video quality ultra ; /uebridge video fps 30"
    # This is a per-launch option. The user's Config/Content and default D3D12 setting are preserved.
    & $editor $project $Level -game -d3d11 -windowed -ResX=1920 -ResY=1080 -log
    if ($LASTEXITCODE -ne 0) { throw "UE exited with code $LASTEXITCODE. Read the UE log; use normal D3D12/JPEG if D3D11 is unsupported." }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
