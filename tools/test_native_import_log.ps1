# Exercise the launcher diagnostic function without starting UE.
$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$path = Join-Path $PSScriptRoot '../unreal/UEBridge/Play-Native.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Launcher syntax errors' }
$function = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-NativeImportFailure' }, $true)
. ([scriptblock]::Create($function.Extent.Text))
$temp = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString('N') + ' import log')
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $log = Join-Path $temp 'NativeImport-test.log'
    $missing = Get-NativeImportFailure -LogPath $log -ExitCode 3
    if (!$missing.Contains($log) -or !$missing.Contains('exit code: 3') -or !$missing.Contains('did not create')) { throw 'Missing-log diagnostic failed' }
    $lines = @('Native automation failed: bad checksum', 'Traceback (most recent call last):', 'ValueError: bad checksum') + (1..150 | ForEach-Object { 'Shutdown filler ' + $_ })
    [IO.File]::WriteAllLines($log, [string[]]$lines, [Text.UTF8Encoding]::new($false))
    $failed = Get-NativeImportFailure -LogPath $log -ExitCode 0
    if (!$failed.Contains('ValueError: bad checksum') -or !$failed.Contains('Shutdown filler 150')) { throw 'Error before long shutdown was lost' }
    [IO.File]::WriteAllText($log, 'Editor initialization stopped before Python', [Text.UTF8Encoding]::new($false))
    $early = Get-NativeImportFailure -LogPath $log -ExitCode 1
    if (!$early.Contains('Editor initialization stopped before Python')) { throw 'Early engine failure was lost' }
    Write-Host 'Native import diagnostics: 3 scenarios passed; launcher syntax passed.'
} finally { Remove-Item -LiteralPath $temp -Recurse -Force }
