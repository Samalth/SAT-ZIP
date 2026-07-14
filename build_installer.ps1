# Build the SAT-ZIP Windows installer (Inno Setup), mirroring SAT-OBELISK's flow:
# 1) build the Release binaries (CLI + GUI) via CMake, 2) compile installer\satzip.iss.
$root = $PSScriptRoot

cmake -S "$root" -B "$root\build" -A x64 -DSATZIP_BUILD_GUI=ON
if ($LASTEXITCODE -ne 0) { Write-Host "CMake configure failed"; exit 1 }
cmake --build "$root\build" --config Release --target satzip_cli satzip_gui
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed"; exit 1 }

# version comes from CMake project() — the single source of truth
$ver = ((Select-String -Path "$root\build\CMakeCache.txt" -Pattern '^CMAKE_PROJECT_VERSION:').Line -split '=')[1]
if (-not $ver) { Write-Host "Could not read version from CMakeCache.txt"; exit 1 }

$candidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
$iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    Write-Host "Inno Setup not found. Install it:  winget install JRSoftware.InnoSetup"
    exit 1
}
& $iscc "/DAppVersion=$ver" "$root\installer\satzip.iss"
if ($LASTEXITCODE -ne 0) { Write-Host "ISCC failed"; exit 1 }
Write-Host "`nInstaller: installer\dist\SATZ-Setup-$ver.exe"
