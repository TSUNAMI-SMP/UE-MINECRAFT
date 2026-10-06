param([string]$EngineRoot = "")
$ErrorActionPreference = "Stop"
try {
    $project = Join-Path $PSScriptRoot "UEBridge.uproject"
    if (!(Test-Path $project) -or !(Test-Path (Join-Path $PSScriptRoot "Source\UEBridge\BridgeProtocol.h"))) {
        throw "Place this script next to UEBridge.uproject in the project you use."
    }
    if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
        throw "Save your level and close Unreal Editor before building. No process was stopped."
    }
    $descriptor = Get-Content -LiteralPath $project -Raw | ConvertFrom-Json
    if (!($descriptor.Modules | Where-Object { $_.Name -eq "UEBridge" })) { throw "This is not the UEBridge project." }
    $association = [string]$descriptor.EngineAssociation
    if (!$EngineRoot) {
        foreach ($key in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association", "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$association")) {
            if (Test-Path $key) {
                $candidate = (Get-ItemProperty -LiteralPath $key).InstalledDirectory
                if ($candidate -and (Test-Path (Join-Path $candidate "Engine\Build\BatchFiles\Build.bat"))) { $EngineRoot = $candidate; break }
            }
        }
        $builds = "HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds"
        if (!$EngineRoot -and (Test-Path $builds)) {
            $properties = (Get-ItemProperty -LiteralPath $builds).PSObject.Properties
            foreach ($property in $properties) {
                if ($property.Name -eq $association -and (Test-Path (Join-Path ([string]$property.Value) "Engine\Build\BatchFiles\Build.bat"))) {
                    $EngineRoot = [string]$property.Value; break
                }
            }
        }
    }
    if (!$EngineRoot) { $EngineRoot = (Read-Host "Enter the Unreal Engine installation folder (example C:\Program Files\Epic Games\UE_5.8)").Trim('"') }
    $buildCommand = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"
    if (!(Test-Path $buildCommand)) { throw "Build.bat was not found in this Unreal Engine installation." }
    # Merge this required engine plugin into the user's descriptor. Keep all other settings.
    $plugin = @($descriptor.Plugins | Where-Object { $_.Name -eq "ProceduralMeshComponent" })
    if ($plugin.Count -eq 0 -or !$plugin[0].Enabled) {
        $backup = "$project.before-player-0.7.0"
        if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $project -Destination $backup }
        if ($plugin.Count -eq 0) {
            $plugins = @($descriptor.Plugins) + @([pscustomobject]@{Name="ProceduralMeshComponent"; Enabled=$true})
            $descriptor | Add-Member -MemberType NoteProperty -Name Plugins -Value $plugins -Force
        } else { $plugin[0] | Add-Member -MemberType NoteProperty -Name Enabled -Value $true -Force }
        $temporary = "$project.player-update.tmp"
        [IO.File]::WriteAllText($temporary, ($descriptor | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $temporary -Destination $project -Force
        Write-Host "Enabled the engine's ProceduralMeshComponent plugin. Original descriptor backup: $backup"
    }
    Write-Host "Building the project you selected: $project"
    & $buildCommand UEBridgeEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE). Read the first error above. Do not launch the old DLL." }
    Write-Host "BUILD SUCCESSFUL. Open this same UEBridge.uproject, load your saved level, and press Play." -ForegroundColor Green
    exit 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
