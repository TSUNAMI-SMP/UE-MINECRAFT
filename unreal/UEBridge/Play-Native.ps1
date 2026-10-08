param([string]$Manifest = "", [string]$EngineRoot = "", [switch]$Reimport, [switch]$Rebuild)
$ErrorActionPreference = "Stop"
function Get-NativeImportSourceHash([string]$ImportScript) {
    if (!(Test-Path -LiteralPath $ImportScript -PathType Leaf)) { throw "import_native_play.py is missing. Extract all Python helpers from the UE update." }
    $directory = Split-Path -Parent $ImportScript
    # Names and contents identify this importer, independent of installation path/mtime.
    $files = @(Get-ChildItem -LiteralPath $directory -File -Filter '*.py' | Sort-Object Name)
    $hashList = ($files | ForEach-Object { $_.Name + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }) -join "`n"
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($hashList)))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function Test-NativeImportCompletion($Marker, [string]$AttemptId, [string]$Manifest, [string]$ManifestHash, [string]$Level, [int]$ExitCode, [string]$LogPath) {
    if (!$Marker -or $Marker.completed -ne $true -or $Marker.importAttemptId -ne $AttemptId -or $Marker.map -ne '/Game/Bridge/Native/NativePlay' -or $Marker.manifest -ne $Manifest -or $Marker.manifestSha256 -ne $ManifestHash -or !(Test-Path -LiteralPath $Level -PathType Leaf)) { return $false }
    if ((Get-Item -LiteralPath $Level).Length -eq 0) { return $false }
    if ($ExitCode -eq 0) { return $true }
    # Only recover the observed access violation after a fully committed import
    # and logged editor shutdown. Other nonzero exits remain failures.
    if ($ExitCode -ne -1073741819 -or !(Test-Path -LiteralPath $LogPath -PathType Leaf)) { return $false }
    $lines = @(Get-Content -LiteralPath $LogPath -Encoding UTF8 -Tail 2000)
    $phase = 0
    $completion = 'Native setup COMPLETE: map=/Game/Bridge/Native/NativePlay package=' + $Manifest + '. Minecraft can stay closed; launch Play-Native.cmd.'
    foreach ($line in $lines) {
        if ($line -match 'Native automation failed|Native setup FAILED|Traceback|LogPython: Error:|Fatal error:') { return $false }
        if ($phase -eq 0 -and $line.Contains($completion)) { $phase = 1 }
        elseif ($phase -eq 1 -and $line.Contains('Cmd: QUIT_EDITOR')) { $phase = 2 }
        elseif ($phase -eq 2 -and $line.Contains('LogExit: Exiting.')) { $phase = 3 }
        elseif ($phase -eq 3 -and $line.Contains('Log file closed,')) { $phase = 4 }
    }
    return $phase -eq 4
}
function Get-NativeImportFailure([string]$LogPath, [int]$ExitCode) {
    $detail = "Native import did not complete (editor exit code: $ExitCode). Import log: $LogPath"
    if (Test-Path -LiteralPath $LogPath) {
        $lines = @(Get-Content -LiteralPath $LogPath -Encoding UTF8)
        $errors = @(Select-String -LiteralPath $LogPath -Encoding UTF8 -Pattern 'Native automation failed|Native setup FAILED|Traceback|LogPython: Error:|Fatal error:' -Context 2,20)
        if ($errors.Count) {
            $detail += "`nImport errors:`n" + (($errors | ForEach-Object { $_.ToString() }) -join "`n")
        }
        # Include the end of the log even if Python never started.
        $tail = @($lines | Select-Object -Last 80) -join "`n"
        $detail += "`nLast 80 log lines:`n$tail"
    } else {
        $detail += "`nThe editor did not create this log. Check the editor launch/path above."
    }
    return $detail
}
try {
    $project = Join-Path $PSScriptRoot "UEBridge.uproject"
    if (!(Test-Path -LiteralPath $project)) { throw "Place Play-Native.cmd and Play-Native.ps1 next to your UEBridge.uproject." }
    if (Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
        throw "Save and close Unreal Editor/the previous native game before starting. Your processes were left running."
    }
    $descriptor = Get-Content -LiteralPath $project -Raw | ConvertFrom-Json
    if (!($descriptor.Modules | Where-Object { $_.Name -eq "UEBridge" })) { throw "This is not the UEBridge project." }
    $association = [string]$descriptor.EngineAssociation
    if (!$EngineRoot) {
        foreach ($key in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association", "HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$association")) {
            if (Test-Path $key) {
                $candidate = (Get-ItemProperty -LiteralPath $key).InstalledDirectory
                if ($candidate -and (Test-Path (Join-Path $candidate "Engine\Binaries\Win64\UnrealEditor.exe"))) { $EngineRoot = $candidate; break }
            }
        }
        $builds = "HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds"
        if (!$EngineRoot -and (Test-Path $builds)) {
            foreach ($property in (Get-ItemProperty -LiteralPath $builds).PSObject.Properties) {
                if ($property.Name -eq $association) { $EngineRoot = [string]$property.Value; break }
            }
        }
        if (!$EngineRoot) {
            $candidate = Join-Path ${env:ProgramFiles} "Epic Games\UE_$association"
            if (Test-Path (Join-Path $candidate "Engine\Binaries\Win64\UnrealEditor.exe")) { $EngineRoot = $candidate }
        }
    }
    if (!$EngineRoot) { $EngineRoot = (Read-Host "Unreal Engine folder (example C:\Program Files\Epic Games\UE_5.8)").Trim('"') }
    $editor = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
    if (!(Test-Path -LiteralPath $editor)) { throw "UnrealEditor.exe was not found in this engine folder." }
    $saved = Join-Path $PSScriptRoot "Saved"
    $markerPath = Join-Path $saved "NativeLauncher.json"
    $marker = $null
    if (Test-Path -LiteralPath $markerPath) {
        try { $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json } catch { Write-Host "Previous native setup marker is unreadable; select your export again." }
    }
    if (!$Manifest -and $marker -and $marker.completed -and !$Reimport) { $Manifest = [string]$marker.manifest }
    if (!$Manifest) {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Title = "Select native_manifest.json created by /uebridge native export"
        $dialog.Filter = "Native Minecraft export (native_manifest.json)|native_manifest.json|JSON files (*.json)|*.json"
        if (Test-Path "C:\UEBridgeTest\MC-Test\uebridge-export") { $dialog.InitialDirectory = "C:\UEBridgeTest\MC-Test\uebridge-export" }
        if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw "No export was selected; the project was not changed." }
        $Manifest = $dialog.FileName
    }
    $Manifest = [IO.Path]::GetFullPath($Manifest.Trim('"'))
    if (!(Test-Path -LiteralPath $Manifest)) { throw "Export not found: $Manifest. Run /uebridge native export in Minecraft and select its native_manifest.json." }
    $nativeData = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    if ($nativeData.schema -ne "uebridge.native.v1" -or !$nativeData.world.complete) { throw "This is not a completed native export. Select native_manifest.json from a successful export." }
    $manifestHash = (Get-FileHash -LiteralPath $Manifest -Algorithm SHA256).Hash.ToLowerInvariant()
    $source = Join-Path $PSScriptRoot "Source"
    $sourceFiles = @(Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object { $_.Extension -in @(".h", ".cpp", ".cs") } | Sort-Object FullName)
    if (!$sourceFiles.Count) { throw "Native-play source files are missing. Extract the complete UE update into this project folder." }
    $hashList = ($sourceFiles | ForEach-Object { $_.FullName.Substring($PSScriptRoot.Length) + ":" + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }) -join "`n"
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $sourceHash = ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($hashList)))).Replace("-", "").ToLowerInvariant() } finally { $algorithm.Dispose() }
    $buildMarker = Join-Path $saved "NativeBuild.sha256"
    $dll = Join-Path $PSScriptRoot "Binaries\Win64\UnrealEditor-UEBridge.dll"
    $built = if (Test-Path -LiteralPath $buildMarker) { (Get-Content -LiteralPath $buildMarker -Raw).Trim() } else { "" }
    if ($Rebuild -or $built -ne $sourceHash -or !(Test-Path -LiteralPath $dll)) {
        Write-Host "First run/update: building the native player. This can take several minutes."
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "Build-UEBridge.ps1") -EngineRoot $EngineRoot
        if ($LASTEXITCODE -ne 0) { throw "Native player build failed. See the first compiler error above; the old game was not launched." }
        New-Item -ItemType Directory -Path $saved -Force | Out-Null
        [IO.File]::WriteAllText($buildMarker, $sourceHash, [Text.UTF8Encoding]::new($false))
        $Reimport = $true
    }
    $importScript = Join-Path $PSScriptRoot "import_native_play.py"
    if (!(Test-Path -LiteralPath $importScript)) { $importScript = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\..\tools\import_native_play.py")) }
    $importHash = Get-NativeImportSourceHash -ImportScript $importScript
    $importSourceMarker = Join-Path $saved "NativeImportSource.sha256"
    $importedHash = if (Test-Path -LiteralPath $importSourceMarker) { (Get-Content -LiteralPath $importSourceMarker -Raw).Trim() } else { "" }
    $ready = $marker -and $marker.completed -and $marker.manifest -eq $Manifest -and $marker.manifestSha256 -eq $manifestHash -and $marker.map -eq "/Game/Bridge/Native/NativePlay" -and $importedHash -eq $importHash
    $level = Join-Path $PSScriptRoot "Content\Bridge\Native\NativePlay.umap"
    if ($Reimport -or !$ready -or !(Test-Path -LiteralPath $level)) {
        Write-Host "Importing this exact export into a separate native-play level: $Manifest"
        $attemptId = [Guid]::NewGuid().ToString("N")
        $logs = Join-Path $saved "Logs"
        New-Item -ItemType Directory -Path $logs -Force | Out-Null
        $importLog = Join-Path $logs ("NativeImport-" + (Get-Date -Format "yyyyMMdd-HHmmss") + "-" + $attemptId + ".log")
        Write-Host "Your existing levels are preserved. Dedicated import log: $importLog"
        $previousManifest = $env:UEBRIDGE_NATIVE_MANIFEST
        $previousAutomation = $env:UEBRIDGE_NATIVE_AUTOMATION
        $previousAttempt = $env:UEBRIDGE_NATIVE_ATTEMPT
        try {
            $env:UEBRIDGE_NATIVE_MANIFEST = $Manifest
            $env:UEBRIDGE_NATIVE_AUTOMATION = "1"
            $env:UEBRIDGE_NATIVE_ATTEMPT = $attemptId
            $importArguments = @("`"$project`"", "-ExecutePythonScript=`"$importScript`"", "-unattended", "-nosplash", "-NoSound", "-abslog=`"$importLog`"")
            $process = Start-Process -FilePath $editor -ArgumentList $importArguments -Wait -PassThru
        } finally {
            $env:UEBRIDGE_NATIVE_MANIFEST = $previousManifest
            $env:UEBRIDGE_NATIVE_AUTOMATION = $previousAutomation
            $env:UEBRIDGE_NATIVE_ATTEMPT = $previousAttempt
        }
        $marker = $null
        if (Test-Path -LiteralPath $markerPath) {
            try { $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json } catch { Write-Host "Import completion marker is unreadable." }
        }
        if (!(Test-NativeImportCompletion -Marker $marker -AttemptId $attemptId -Manifest $Manifest -ManifestHash $manifestHash -Level $level -ExitCode $process.ExitCode -LogPath $importLog)) {
            throw (Get-NativeImportFailure -LogPath $importLog -ExitCode $process.ExitCode)
        }
        if ($process.ExitCode -ne 0) {
            Write-Warning "Native import was committed and the editor finished its logged shutdown, but exited with access violation $($process.ExitCode). Starting the saved native map. Import log: $importLog"
        }
        [IO.File]::WriteAllText($importSourceMarker, $importHash, [Text.UTF8Encoding]::new($false))
    }
    Write-Host "Starting UE native play. Minecraft can stay closed. Saved world: Saved\NativeWorlds." -ForegroundColor Green
    $playArguments = @("`"$project`"", "/Game/Bridge/Native/NativePlay", "-game", "-windowed", "-ResX=1280", "-ResY=720")
    Start-Process -FilePath $editor -ArgumentList $playArguments | Out-Null
    exit 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
