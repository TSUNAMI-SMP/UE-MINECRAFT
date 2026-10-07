# Run the actual build-helper merge function in isolation. No editor/build is started.
$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot '..\unreal\UEBridge\Build-UEBridge.ps1'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'Build helper PowerShell syntax errors' }
$function = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Enable-UEBridgeRequiredPlugins' }, $true)
if (!$function) { throw 'Required-plugin merge function was not found' }
. ([scriptblock]::Create($function.Extent.Text))

function Assert-True($value, [string]$message) {
    if (!$value) { throw $message }
}

$descriptor = @'
{
  "FileVersion": 3,
  "EngineAssociation": "user-engine-guid",
  "Modules": [{"Name": "UEBridge", "Type": "Runtime", "LoadingPhase": "Default"}],
  "Custom": {"Keep": [1, 2, {"UnknownSetting": "preserved"}]},
  "Plugins": [
    {"Name": "PythonScriptPlugin", "Enabled": false, "SupportedTargetPlatforms": ["Win64"], "Optional": true},
    {"Name": "ProceduralMeshComponent", "Enabled": true},
    {"Name": "UserPlugin", "Enabled": false, "MarketplaceURL": "user-settings"}
  ]
}
'@ | ConvertFrom-Json
$required = @('Niagara', 'GeometryCollectionPlugin', 'PythonScriptPlugin', 'EditorScriptingUtilities', 'ProceduralMeshComponent')
$merged = Enable-UEBridgeRequiredPlugins -Descriptor $descriptor
Assert-True $merged.Changed 'Missing/disabled plugins were not repaired'
Assert-True ($descriptor.EngineAssociation -eq 'user-engine-guid') 'Engine association changed'
Assert-True ($descriptor.Modules[0].LoadingPhase -eq 'Default') 'Module settings changed'
Assert-True ($descriptor.Custom.Keep[2].UnknownSetting -eq 'preserved') 'Custom nested settings changed'
foreach ($name in $required) {
    $plugin = @($descriptor.Plugins | Where-Object { $_.Name -eq $name })
    Assert-True ($plugin.Count -eq 1 -and $plugin[0].Enabled) "Required plugin not enabled exactly once: $name"
}
$python = @($descriptor.Plugins | Where-Object { $_.Name -eq 'PythonScriptPlugin' })[0]
Assert-True ($python.Optional -and $python.SupportedTargetPlatforms[0] -eq 'Win64') 'Existing required-plugin metadata changed'
$custom = @($descriptor.Plugins | Where-Object { $_.Name -eq 'UserPlugin' })[0]
Assert-True (!$custom.Enabled -and $custom.MarketplaceURL -eq 'user-settings') 'Unrelated plugin settings changed'
Assert-True (!(Enable-UEBridgeRequiredPlugins -Descriptor $descriptor).Changed) 'Second merge was not idempotent'

$absent = '{"EngineAssociation":"5.8","Modules":[],"UserSettings":{"Keep":true}}' | ConvertFrom-Json
$empty = Enable-UEBridgeRequiredPlugins -Descriptor $absent
Assert-True ($empty.Changed -and $absent.Plugins.Count -eq $required.Count -and $absent.UserSettings.Keep) 'Missing Plugins property was not merged safely'

$duplicate = '{"Plugins":[{"Name":"Niagara","Enabled":false},{"Name":"Niagara","Enabled":true}]}' | ConvertFrom-Json
$rejected = $false
try { Enable-UEBridgeRequiredPlugins -Descriptor $duplicate | Out-Null } catch { $rejected = $true }
Assert-True $rejected 'Duplicate required plugin records were accepted'
Write-Host 'Native build plugin merge fixtures passed; user settings preserved.'
