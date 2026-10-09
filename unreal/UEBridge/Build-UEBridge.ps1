param([string]$EngineRoot = "")
$ErrorActionPreference = "Stop"

function Enable-UEBridgeRequiredPlugins {
    param([Parameter(Mandatory=$true)]$Descriptor, [bool]$NiagaraFluidsAvailable=$false)
    # Only required plugin Enabled flags are merged. Engine association, project
    # modules, custom settings and existing plugin metadata remain intact.
    $requiredPlugins = @('Niagara', 'GeometryCollectionPlugin', 'PythonScriptPlugin', 'EditorScriptingUtilities', 'ProceduralMeshComponent')
    if ($NiagaraFluidsAvailable) { $requiredPlugins += 'NiagaraFluids' }
    $plugins = @($Descriptor.Plugins | Where-Object { $null -ne $_ })
    $changed = $false
    $enabled = @()
    foreach ($name in $requiredPlugins) {
        $existing = @($plugins | Where-Object { $_.Name -eq $name })
        if ($existing.Count -gt 1) { throw "Duplicate required plugin entries: $name. The project descriptor was not changed." }
        if ($existing.Count -eq 0) {
            $plugins += [pscustomobject]@{Name=$name; Enabled=$true}
            $changed = $true
            $enabled += $name
        } elseif ($existing[0].Enabled -ne $true) {
            $existing[0] | Add-Member -MemberType NoteProperty -Name Enabled -Value $true -Force
            $changed = $true
            $enabled += $name
        }
    }
    if ($changed) { $Descriptor | Add-Member -MemberType NoteProperty -Name Plugins -Value $plugins -Force }
    return [pscustomobject]@{Descriptor=$Descriptor; Changed=$changed; Enabled=$enabled}
}

try {
    $project = Join-Path $PSScriptRoot "UEBridge.uproject"
    if (!(Test-Path $project) -or !(Test-Path (Join-Path $PSScriptRoot "Source\UEBridge\BridgeProtocol.h"))) {
        throw "Place this script next to UEBridge.uproject in the project you use."
    }
    if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
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
    $niagaraFluids = (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine\Plugins\FX\NiagaraFluids\NiagaraFluids.uplugin')) -or (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine\Plugins\Experimental\NiagaraFluids\NiagaraFluids.uplugin'))
    $merged = Enable-UEBridgeRequiredPlugins -Descriptor $descriptor -NiagaraFluidsAvailable $niagaraFluids
    if (!$niagaraFluids) { Write-Host 'Niagara Fluids templates are not installed. The procedural explosion fallback will be available.' }
    if ($merged.Changed) {
        $identity = [Guid]::NewGuid().ToString('N')
        $backup = "$project.before-native-0.12.0-$identity"
        $temporary = "$project.native-update-$identity.tmp"
        try {
            [IO.File]::WriteAllText($temporary, ($merged.Descriptor | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
            # Same-directory replacement preserves a complete old descriptor as
            # backup and publishes a complete merged descriptor atomically.
            [IO.File]::Replace($temporary, $project, $backup)
        } finally {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
        Write-Host "Enabled required engine plugins: $($merged.Enabled -join ', '). Original descriptor backup: $backup"
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
