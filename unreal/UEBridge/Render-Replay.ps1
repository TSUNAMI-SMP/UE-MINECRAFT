param([string]$Replay = "", [string]$EngineRoot = "", [ValidateRange(160,3840)][int]$Width = 1280, [ValidateRange(90,2160)][int]$Height = 720)
$ErrorActionPreference = 'Stop'
function Read-ReplayManifest([string]$Path) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'Select the replay.json saved by /record stop, not native_manifest.json.' }
    $full = (Resolve-Path -LiteralPath $Path).ProviderPath
    if ((Get-Item -LiteralPath $full).Length -gt 1048576) { throw 'Replay manifest exceeds 1 MiB.' }
    $recording = Get-Content -LiteralPath $full -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($recording.version -ne 1 -or $recording.renderFps -ne 60 -or $recording.audio -ne $false -or $recording.frames -lt 2 -or $recording.frames -gt 3000 -or $recording.frames -ne [Math]::Floor($recording.frames) -or $recording.seconds -le 0 -or $recording.seconds -gt 120 -or [double]::IsNaN($recording.seconds) -or [double]::IsInfinity($recording.seconds) -or !$recording.package) { throw 'Invalid/incomplete replay manifest.' }
    $directory = Split-Path -Parent $full
    foreach ($entry in @(@('baseline.ndjson', 'baselineMd5'), @('frames.ubrf', 'framesMd5'))) {
        $file = Join-Path $directory $entry[0]
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Replay file is missing: $file" }
        $bytes = (Get-Item -LiteralPath $file).Length
        if ($bytes -lt 1 -or $bytes -gt 536870912) { throw "Replay file size limit: $file" }
        $expected = [string]$recording.($entry[1])
        if ($expected -notmatch '^[0-9a-fA-F]{32}$' -or (Get-FileHash -LiteralPath $file -Algorithm MD5).Hash -ne $expected) { throw "Replay checksum mismatch: $file" }
    }
    return $recording
}
try {
    if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) { throw 'Save and close the game/editor first. Existing processes were left running.' }
    if (!$Replay) {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Title = 'Select Saved\Cinematics\...\replay.json'
        $dialog.Filter = 'UEBridge recording (replay.json)|replay.json'
        $dialog.InitialDirectory = Join-Path $PSScriptRoot 'Saved\Cinematics'
        if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
        $Replay = $dialog.FileName
        $dialog.Dispose()
    }
    $Replay = (Resolve-Path -LiteralPath $Replay).ProviderPath
    $recording = Read-ReplayManifest $Replay
    $project = Join-Path $PSScriptRoot 'UEBridge.uproject'
    $markerPath = Join-Path $PSScriptRoot 'Saved\NativeLauncher.json'
    if (!(Test-Path -LiteralPath $markerPath -PathType Leaf)) { throw 'Run Play-Native.cmd once to import/build this export before rendering.' }
    $marker = Get-Content -LiteralPath $markerPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($marker.completed -ne $true -or $marker.packageId -ne $recording.package -or $marker.map -ne '/Game/Bridge/Native/NativePlay') { throw 'Import the exact export used for this recording with Play-Native.cmd. Your live saves were not changed.' }
    $descriptor = Get-Content -LiteralPath $project -Raw -Encoding UTF8 | ConvertFrom-Json
    if (!$EngineRoot) {
        foreach ($key in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$($descriptor.EngineAssociation)", "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$($descriptor.EngineAssociation)")) {
            if (Test-Path $key) { $candidate = (Get-ItemProperty -LiteralPath $key).InstalledDirectory; if ($candidate -and (Test-Path (Join-Path $candidate 'Engine\Binaries\Win64\UnrealEditor.exe'))) { $EngineRoot = $candidate; break } }
        }
        if (!$EngineRoot) { $EngineRoot = Join-Path ${env:ProgramFiles} "Epic Games\UE_$($descriptor.EngineAssociation)" }
    }
    $editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    if (!(Test-Path -LiteralPath $editor -PathType Leaf)) { throw 'UnrealEditor.exe not found. Specify -EngineRoot with your installed UE folder.' }
    $log = Join-Path $PSScriptRoot ('Saved\Logs\NativeReplay-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
    $arguments = @("`"$project`"", '/Game/Bridge/Native/NativePlay', '-game', '-windowed', "-ResX=$Width", "-ResY=$Height", "-BridgeNativeReplay=`"$Replay`"", "-abslog=`"$log`"")
    Write-Host 'Re-rendering at fixed 60fps. This may take longer than the recording. Do not resize the render window. Audio is not included.' -ForegroundColor Green
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -Wait -PassThru
    $video = Join-Path (Split-Path -Parent $Replay) 'video-60fps.avi'
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $video -PathType Leaf) -or !(Select-String -LiteralPath $log -SimpleMatch 'Bridge cinematic COMPLETE:' -Quiet)) { throw "Render did not complete (exit $($process.ExitCode)). Log: $log. Live saves are untouched." }
    Write-Host "60fps video: $video" -ForegroundColor Green
    exit 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
