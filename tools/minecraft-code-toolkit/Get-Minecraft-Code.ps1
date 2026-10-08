param(
    [string]$JavaHome,
    [string]$PythonExe,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$CacheRoot,
    [switch]$SkipGeneration
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = $PSScriptRoot
$PreviousJava = $env:JAVA_HOME
$PreviousCache = $env:GRADLE_USER_HOME
$TranscriptStarted = $false

function Find-Java21 {
    param([string]$Requested)
    $Candidates = @()
    if ($Requested) { $Candidates += $Requested }
    elseif ($env:JAVA_HOME) { $Candidates += $env:JAVA_HOME }
    if (-not $Requested) {
        $JavaCommand = Get-Command java -CommandType Application -ErrorAction SilentlyContinue
        if ($JavaCommand) { $Candidates += Split-Path (Split-Path $JavaCommand.Source -Parent) -Parent }
        if ($env:ProgramFiles) {
            foreach ($Vendor in @('Microsoft', 'Eclipse Adoptium', 'Java', 'Amazon Corretto')) {
                $VendorPath = Join-Path $env:ProgramFiles $Vendor
                if (Test-Path -LiteralPath $VendorPath) {
                    $Candidates += @(Get-ChildItem -LiteralPath $VendorPath -Directory | Where-Object { $_.Name -match '(jdk|jre|corretto).*21' } | ForEach-Object { $_.FullName })
                }
            }
        }
    }
    foreach ($Candidate in $Candidates) {
        $Java = Join-Path $Candidate 'bin/java.exe'
        $Javac = Join-Path $Candidate 'bin/javac.exe'
        if ($env:OS -ne 'Windows_NT') { $Java = Join-Path $Candidate 'bin/java'; $Javac = Join-Path $Candidate 'bin/javac' }
        if (-not (Test-Path -LiteralPath $Java) -or -not (Test-Path -LiteralPath $Javac)) { continue }
        $OldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { $Version = (& $Java -version 2>&1 | Out-String); $Code = $LASTEXITCODE }
        finally { $ErrorActionPreference = $OldPreference }
        if ($Code -eq 0 -and $Version -match 'version "21[.+"]') { return $Candidate }
    }
    throw 'Java 21 JDK was not found. Install a Java 21 JDK or run with -JavaHome "C:\path\to\jdk-21". The folder must contain bin\java.exe and bin\javac.exe.'
}

function Find-Python {
    param([string]$Requested)
    $Candidates = @()
    if ($Requested) { $Candidates += $Requested }
    else {
        $Candidates += Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
        foreach ($Name in @('python', 'python3')) {
            $Command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue
            if ($Command) { $Candidates += $Command.Source }
        }
    }
    foreach ($Candidate in $Candidates) {
        if (-not (Test-Path -LiteralPath $Candidate)) { continue }
        $OldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & $Candidate -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>$null; $Code = $LASTEXITCODE }
        finally { $ErrorActionPreference = $OldPreference }
        if ($Code -eq 0) { return $Candidate }
    }
    throw 'Python 3.10+ was not found. UE 5.8 bundled Python can be used via -EngineRoot, or supply -PythonExe "C:\path\to\python.exe". No pip packages are needed.'
}

try {
    $Work = Join-Path $Root '.work'
    New-Item -ItemType Directory -Path $Work -Force | Out-Null
    $RunId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $Log = Join-Path $Work ('Get-Code-' + $RunId + '.log')
    Start-Transcript -LiteralPath $Log | Out-Null
    $TranscriptStarted = $true
    $Python = Find-Python $PythonExe
    if (-not $SkipGeneration) {
        $env:JAVA_HOME = Find-Java21 $JavaHome
        if (-not $CacheRoot) { $CacheRoot = Join-Path $Work 'gradle' }
        $env:GRADLE_USER_HOME = [IO.Path]::GetFullPath($CacheRoot)
        Write-Host 'Generating Minecraft 1.21.11 sources with Yarn build.6 and Loom/Vineflower.'
        Write-Host 'The first run downloads tools and game inputs. Decompilation may take several minutes.'
        Write-Host ('Java: ' + $env:JAVA_HOME)
        Push-Location -LiteralPath $Root
        try {
            $Wrapper = Join-Path $Root 'gradlew.bat'
            if ($env:OS -ne 'Windows_NT') { $Wrapper = Join-Path $Root 'gradlew' }
            & $Wrapper stageReferenceInputs --no-daemon --max-workers=2 --console=plain
            if ($LASTEXITCODE -ne 0) { throw ('Source generation failed (exit ' + $LASTEXITCODE + '). See the first Gradle error in ' + $Log) }
        } finally { Pop-Location }
    }
    $Output = Join-Path $Root ('results/Minecraft-Reference-1.21.11-' + $RunId)
    & $Python (Join-Path $Root 'collect_reference.py') --inputs (Join-Path $Root 'build/reference-inputs') --output $Output
    if ($LASTEXITCODE -ne 0) { throw ('Reference collection failed (exit ' + $LASTEXITCODE + '). See ' + $Log) }
    Write-Host ''
    Write-Host ('SUCCESS: ' + $Output)
    Write-Host ('Read: ' + (Join-Path $Output 'README.md'))
    Write-Host ('Selected sources and assets: ' + (Join-Path $Output 'Minecraft-Reference-selected.zip'))
    Write-Host 'Game mods, saves and UE source files were not changed.'
    Write-Host ('Log: ' + $Log)
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
} finally {
    if ($TranscriptStarted) { Stop-Transcript | Out-Null }
    $env:JAVA_HOME = $PreviousJava
    $env:GRADLE_USER_HOME = $PreviousCache
}
