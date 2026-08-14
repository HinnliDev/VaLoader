[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$GameDirectory,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$AndroidSdk = "",
    [string]$SevenZip = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$GameDirectory = [IO.Path]::GetFullPath($GameDirectory)

function Resolve-AndroidSdk {
    $candidates = @(
        $AndroidSdk,
        $env:ANDROID_SDK_ROOT,
        $env:ANDROID_HOME,
        (Join-Path ([Environment]::GetFolderPath("LocalApplicationData")) "Android\Sdk")
    )
    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Container)) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    throw "Android SDK was not found. Set ANDROID_SDK_ROOT or pass -AndroidSdk."
}

function Resolve-SevenZip {
    if (-not [string]::IsNullOrWhiteSpace($SevenZip)) {
        if (Test-Path -LiteralPath $SevenZip -PathType Leaf) {
            return [IO.Path]::GetFullPath($SevenZip)
        }
        throw "7-Zip executable was not found: $SevenZip"
    }
    foreach ($commandName in @("7z.exe", "7zz.exe")) {
        $command = Get-Command $commandName -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles "7-Zip\7z.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "7-Zip\7z.exe")
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }
    throw "7-Zip is required. Install it or pass -SevenZip."
}

$requiredDex = @("classes.dex", "classes2.dex", "classes3.dex")
foreach ($dexName in $requiredDex) {
    $dexPath = Join-Path $GameDirectory $dexName
    if (-not (Test-Path -LiteralPath $dexPath -PathType Leaf)) {
        throw "Missing game DEX: $dexPath"
    }
}
$bootstrapLibrary = Join-Path $GameDirectory "lib\arm64-v8a\libtprt.so"
if (-not (Test-Path -LiteralPath $bootstrapLibrary -PathType Leaf)) {
    throw "Missing game bootstrap library: $bootstrapLibrary"
}

$sdkRoot = Resolve-AndroidSdk
$env:ANDROID_SDK_ROOT = $sdkRoot
$env:ANDROID_HOME = $sdkRoot
$sevenZipExecutable = Resolve-SevenZip

$buildToolsRoot = Join-Path $sdkRoot "build-tools"
$buildTools = Get-ChildItem -LiteralPath $buildToolsRoot -Directory |
    Where-Object {
        (Test-Path -LiteralPath (Join-Path $_.FullName "zipalign.exe") -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $_.FullName "apksigner.bat") -PathType Leaf)
    } |
    Sort-Object @{
        Expression = {
            try { [version]$_.Name } catch { [version]"0.0" }
        }
    } -Descending |
    Select-Object -First 1
if ($null -eq $buildTools) {
    throw "No Android build-tools installation with zipalign and apksigner was found."
}

$gradleTask = if ($Configuration -eq "Debug") { ":app:assembleDebug" } else { ":app:assembleRelease" }
& (Join-Path $projectRoot "gradlew.bat") $gradleTask
if ($LASTEXITCODE -ne 0) {
    throw "Gradle build failed with exit code $LASTEXITCODE"
}

$variant = $Configuration.ToLowerInvariant()
$baseApk = Join-Path $projectRoot "app\build\outputs\apk\$variant\app-$variant.apk"
if (-not (Test-Path -LiteralPath $baseApk -PathType Leaf)) {
    throw "Base APK was not produced: $baseApk"
}

$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot "app\build"))
$stage = [IO.Path]::GetFullPath((Join-Path $buildRoot "valoader-package"))
if (-not $stage.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe staging path: $stage"
}
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage | Out-Null

try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($baseApk, $stage)
    $signatureDirectory = Join-Path $stage "META-INF"
    if (Test-Path -LiteralPath $signatureDirectory) {
        Remove-Item -LiteralPath $signatureDirectory -Recurse -Force
    }

    $existingDex = Get-ChildItem -LiteralPath $stage -File -Filter "classes*.dex"
    $highestDexIndex = 0
    foreach ($file in $existingDex) {
        if ($file.Name -eq "classes.dex") {
            $highestDexIndex = [Math]::Max($highestDexIndex, 1)
        } elseif ($file.Name -match '^classes(\d+)\.dex$') {
            $highestDexIndex = [Math]::Max($highestDexIndex, [int]$Matches[1])
        }
    }
    foreach ($sourceName in $requiredDex) {
        $highestDexIndex++
        $destinationName = "classes$highestDexIndex.dex"
        Copy-Item -LiteralPath (Join-Path $GameDirectory $sourceName) -Destination (Join-Path $stage $destinationName)
        Write-Host "Added $sourceName as $destinationName"
    }

    $nativeStage = Join-Path $stage "lib\arm64-v8a"
    New-Item -ItemType Directory -Force -Path $nativeStage | Out-Null
    Copy-Item -LiteralPath $bootstrapLibrary -Destination (Join-Path $nativeStage "libtprt.so") -Force
    Write-Host "Added bootstrap library libtprt.so"

    $unsignedApk = Join-Path $buildRoot "Valoader-unsigned.apk"
    $alignedApk = Join-Path $buildRoot "Valoader-aligned.apk"
    $outputDirectory = Join-Path $projectRoot "dist"
    $outputApk = Join-Path $outputDirectory "Valoader-$variant.apk"
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    foreach ($artifact in @($unsignedApk, $alignedApk, $outputApk)) {
        if (Test-Path -LiteralPath $artifact -PathType Leaf) {
            Remove-Item -LiteralPath $artifact -Force
        }
    }

    Push-Location $stage
    try {
        & $sevenZipExecutable a -tzip -mx=0 -mcu=on $unsignedApk ".\*" | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "Could not create unsigned APK"
        }
    } finally {
        Pop-Location
    }

    $zipalign = Join-Path $buildTools.FullName "zipalign.exe"
    $apksigner = Join-Path $buildTools.FullName "apksigner.bat"
    & $zipalign -p -f 4 $unsignedApk $alignedApk
    if ($LASTEXITCODE -ne 0) {
        throw "zipalign failed"
    }

    if ($Configuration -eq "Release") {
        $keystore = $env:VALOADER_KEYSTORE
        $keystoreAlias = $env:VALOADER_KEY_ALIAS
        if ([string]::IsNullOrWhiteSpace($keystore) -or
            [string]::IsNullOrWhiteSpace($keystoreAlias) -or
            [string]::IsNullOrWhiteSpace($env:VALOADER_KEYSTORE_PASSWORD) -or
            [string]::IsNullOrWhiteSpace($env:VALOADER_KEY_PASSWORD)) {
            throw "Release signing requires VALOADER_KEYSTORE, VALOADER_KEY_ALIAS, VALOADER_KEYSTORE_PASSWORD and VALOADER_KEY_PASSWORD."
        }
        & $apksigner sign --ks $keystore --ks-pass env:VALOADER_KEYSTORE_PASSWORD --key-pass env:VALOADER_KEY_PASSWORD --ks-key-alias $keystoreAlias --out $outputApk $alignedApk
    } else {
        $debugKeystore = Join-Path ([Environment]::GetFolderPath("UserProfile")) ".android\debug.keystore"
        if (-not (Test-Path -LiteralPath $debugKeystore -PathType Leaf)) {
            $keytoolPath = if ($env:JAVA_HOME) {
                Join-Path $env:JAVA_HOME "bin\keytool.exe"
            } else {
                (Get-Command keytool.exe -ErrorAction Stop).Source
            }
            New-Item -ItemType Directory -Force -Path (Split-Path $debugKeystore -Parent) | Out-Null
            & $keytoolPath -genkeypair -v -keystore $debugKeystore -storepass android -alias androiddebugkey -keypass android -dname "CN=Android Debug,O=Android,C=US" -keyalg RSA -keysize 2048 -validity 10000
            if ($LASTEXITCODE -ne 0) {
                throw "Could not create debug signing key"
            }
        }
        & $apksigner sign --ks $debugKeystore --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey --out $outputApk $alignedApk
    }
    if ($LASTEXITCODE -ne 0) {
        throw "APK signing failed"
    }
    & $apksigner verify --verbose $outputApk
    if ($LASTEXITCODE -ne 0) {
        throw "APK signature verification failed"
    }

    $result = Get-Item -LiteralPath $outputApk
    Write-Host "Built $($result.FullName)"
    Write-Host "Size: $($result.Length) bytes"
} finally {
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
