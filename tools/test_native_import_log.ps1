# Exercise the launcher diagnostic function without starting UE.
$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$path = Join-Path $PSScriptRoot '../unreal/UEBridge/Play-Native.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Launcher syntax errors' }
$function = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-NativeImportFailure' }, $true)
. ([scriptblock]::Create($function.Extent.Text))
$hashFunction = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-NativeImportSourceHash' }, $true)
. ([scriptblock]::Create($hashFunction.Extent.Text))
$completionFunction = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-NativeImportCompletion' }, $true)
. ([scriptblock]::Create($completionFunction.Extent.Text))
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
    $script = Join-Path $temp 'import_native_play.py'
    $helper = Join-Path $temp 'bridge_lighting_materials.py'
    [IO.File]::WriteAllText($script, '# entry', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($helper, '# first', [Text.UTF8Encoding]::new($false))
    $first = Get-NativeImportSourceHash -ImportScript $script
    (Get-Item -LiteralPath $helper).LastWriteTimeUtc = [DateTime]::UtcNow.AddDays(-1)
    if ((Get-NativeImportSourceHash $script) -ne $first) { throw 'Timestamps unnecessarily invalidate import' }
    $copy = Join-Path $temp 'other installation'
    New-Item -ItemType Directory -Path $copy | Out-Null
    Copy-Item -LiteralPath $script,$helper -Destination $copy
    if ((Get-NativeImportSourceHash (Join-Path $copy 'import_native_play.py')) -ne $first) { throw 'Installation path unnecessarily invalidates import' }
    [IO.File]::WriteAllText($helper, '# fixed', [Text.UTF8Encoding]::new($false))
    $fixed = Get-NativeImportSourceHash $script
    if ($fixed -eq $first) { throw 'Changed helper content did not invalidate import' }
    Remove-Item -LiteralPath $helper
    if ((Get-NativeImportSourceHash $script) -eq $fixed) { throw 'Removed helper did not invalidate import' }
    $missingScriptRejected = $false
    try { Get-NativeImportSourceHash (Join-Path $temp 'missing.py') | Out-Null } catch { $missingScriptRejected = $true }
    if (!$missingScriptRejected) { throw 'Missing entry script was accepted' }
    $level = Join-Path $temp 'NativePlay.umap'
    [IO.File]::WriteAllText($level, 'saved map')
    $marker = [pscustomobject]@{ completed=$true; importAttemptId='current'; map='/Game/Bridge/Native/NativePlay'; manifest='C:\export\native_manifest.json'; manifestSha256='abc' }
    $parameters = @{ Marker=$marker; AttemptId='current'; Manifest=$marker.manifest; ManifestHash='abc'; Level=$level; ExitCode=-1073741819; LogPath=$log }
    $shutdown = @('LogPython: Native setup COMPLETE: map=/Game/Bridge/Native/NativePlay package=C:\export\native_manifest.json. Minecraft can stay closed; launch Play-Native.cmd.', 'Cmd: QUIT_EDITOR', 'LogExit: Exiting.', 'Log file closed, 10/08/26 15:37:46')
    [IO.File]::WriteAllLines($log, $shutdown)
    if (!(Test-NativeImportCompletion @parameters)) { throw 'Committed shutdown access violation was rejected' }
    foreach ($field in @('completed','importAttemptId','map','manifest','manifestSha256')) {
        $old = $marker.$field
        $marker.$field = if ($field -eq 'completed') { $false } else { 'stale' }
        if (Test-NativeImportCompletion @parameters) { throw "Stale marker accepted: $field" }
        $marker.$field = $old
    }
    $parameters.ExitCode = 1
    if (Test-NativeImportCompletion @parameters) { throw 'Unknown exit accepted' }
    $parameters.ExitCode = -1073741819
    foreach ($lines in @(@('early crash'), $shutdown[1..3], $shutdown[0..2], @($shutdown[0],$shutdown[2],$shutdown[1],$shutdown[3]), @($shutdown + 'LogPython: Error: failed'), @($shutdown + 'Fatal error:'))) {
        [IO.File]::WriteAllLines($log, [string[]]$lines)
        if (Test-NativeImportCompletion @parameters) { throw 'Incomplete or failed shutdown accepted' }
    }
    Remove-Item -LiteralPath $log
    if (Test-NativeImportCompletion @parameters) { throw 'Missing recovery log accepted' }
    $parameters.ExitCode = 0
    if (!(Test-NativeImportCompletion @parameters)) { throw 'Normal committed exit rejected' }
    Remove-Item -LiteralPath $level
    if (Test-NativeImportCompletion @parameters) { throw 'Missing map accepted' }
    Write-Host 'Native import diagnostics: 3 scenarios; source fingerprint: 5 scenarios; completion/exit validation scenarios and launcher syntax passed.'
} finally { Remove-Item -LiteralPath $temp -Recurse -Force }
