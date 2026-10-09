$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot '..\unreal\UEBridge\Render-Replay.ps1'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path $source), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$function = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Read-ReplayManifest' }, $true)
if (!$function) { throw 'Replay preflight function missing' }
Invoke-Expression $function.Extent.Text
$temp = Join-Path ([IO.Path]::GetTempPath()) ('uebridge-replay-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $baseline = Join-Path $temp 'baseline.ndjson'; $frames = Join-Path $temp 'frames.ubrf'; $manifest = Join-Path $temp 'replay.json'
    [IO.File]::WriteAllText($baseline, 'baseline'); [IO.File]::WriteAllText($frames, 'UBR1frames')
    $valid = @{version=1;renderFps=60;audio=$false;package='local-package';frames=10;seconds=.5;baselineMd5=(Get-FileHash $baseline -Algorithm MD5).Hash;framesMd5=(Get-FileHash $frames -Algorithm MD5).Hash}
    [IO.File]::WriteAllText($manifest, ($valid | ConvertTo-Json))
    $checked = Read-ReplayManifest $manifest
    if ($checked.renderFps -ne 60 -or $checked.frames -ne 10) { throw 'Valid replay rejected' }
    foreach ($case in @(@('version',2),@('renderFps',30),@('audio',$true),@('frames',1),@('frames',3001),@('frames',2.5),@('seconds',0),@('seconds',121),@('framesMd5','0'*32))) {
        $candidate = $valid.Clone(); $candidate[$case[0]] = $case[1]; [IO.File]::WriteAllText($manifest, ($candidate | ConvertTo-Json))
        $rejected = $false
        try { Read-ReplayManifest $manifest | Out-Null } catch { $rejected = $true }
        if (!$rejected) { throw "Invalid replay accepted: $($case[0])=$($case[1])" }
    }
    [IO.File]::WriteAllText($manifest, ($valid | ConvertTo-Json)); [IO.File]::WriteAllText($frames, 'corrupt')
    $rejected = $false; try { Read-ReplayManifest $manifest | Out-Null } catch { $rejected = $true }
    if (!$rejected) { throw 'Corrupt frames accepted' }
    Write-Host 'Replay launcher parser/preflight: valid input, limits, fps/audio, checksums and corruption checks passed'
} finally {
    # Delete only this tool-created, explicitly named temporary test directory.
    if ($temp -and $temp.StartsWith([IO.Path]::GetTempPath()) -and (Split-Path -Leaf $temp) -like 'uebridge-replay-test-*') { Remove-Item -LiteralPath $temp -Recurse -Force }
}
