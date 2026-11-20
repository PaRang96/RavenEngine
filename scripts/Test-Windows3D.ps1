param(
    [ValidateSet('x64-debug', 'x64-release')]
    [string]$Preset = 'x64-debug',
    [switch]$DefaultLaunch
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if ($DefaultLaunch) {
    & python "$PSScriptRoot/Test-Windows3DLive.py" $Preset
    if ($LASTEXITCODE -ne 0) { throw "The $Preset default-launch check failed." }
    return
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $repo "out/build/$Preset"
$runtime = Join-Path $build 'RavenEngine'
if (!(Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
    throw "Configure $Preset from the Visual Studio developer environment first."
}

& cmake --build $build --target Raven3DSmoke
if ($LASTEXITCODE -ne 0) { throw "Could not build the $Preset smoke test." }

$scratch = Join-Path $repo "out/validation/windows-3d/$Preset-$([Guid]::NewGuid().ToString('N'))"
$source = Join-Path $scratch 'source'
$variant = Join-Path $scratch 'variant'
$gameBuild = Join-Path $scratch 'game-build'
$probe = Join-Path $scratch 'probe'
New-Item -ItemType Directory -Path $source, $variant, $probe, "$source/RavenEngine/Private" -Force | Out-Null
Write-Host "Smoke artifacts are under $scratch"

# Keep the shared contract, but build a body-only game edit in a separate source tree.
Copy-Item -LiteralPath "$repo/CMakeLists.txt", "$repo/CMakePresets.json", "$repo/cmake" -Destination $source -Recurse
Copy-Item -LiteralPath "$repo/RavenEngine/CMakeLists.txt", "$repo/RavenEngine/Public" -Destination "$source/RavenEngine" -Recurse
Copy-Item -LiteralPath "$repo/RavenEngine/Private/Game" -Destination "$source/RavenEngine/Private" -Recurse

$recipe = Get-Content -LiteralPath "$runtime/GameBuild/BuildGame.cmake" -Raw
$repoPath = $repo.Replace('\', '/')
$runtimePath = $runtime.Replace('\', '/')
$sourcePath = $source.Replace('\', '/')
$variantPath = $variant.Replace('\', '/')
$gameBuildPath = $gameBuild.Replace('\', '/')
$recipe = $recipe.Replace("set(root [==[$repoPath]==])", "set(root [==[$sourcePath]==])")
$recipe = $recipe.Replace("-DRAVEN_ROOT=$repoPath]==]", "-DRAVEN_ROOT=$sourcePath]==]")
$recipe = $recipe.Replace(
    "set(build_directory [==[$runtimePath/GameBuild]==])",
    "set(build_directory [==[$gameBuildPath]==])")
$recipe = $recipe.Replace(
    "-DRAVEN_GAME_OUTPUT=$runtimePath]==]",
    "-DRAVEN_GAME_OUTPUT=$variantPath]==]")
if (!$recipe.Contains("set(root [==[$sourcePath]==])") -or
    !$recipe.Contains("-DRAVEN_ROOT=$sourcePath]==]") -or
    !$recipe.Contains("set(build_directory [==[$gameBuildPath]==])") -or
    !$recipe.Contains("-DRAVEN_GAME_OUTPUT=$variantPath]==]")) {
    throw 'Could not isolate the generated game build recipe.'
}
$recipeFile = Join-Path $scratch 'BuildGame.cmake'
$utf8 = [System.Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($recipeFile, $recipe, $utf8)

$gameSource = Join-Path $source 'RavenEngine/Private/Game/InteractionGame.cpp'
$gameBody = [IO.File]::ReadAllText($gameSource)
$originalRule = 'return collected == pickups.size();'
$changedRule = 'return collected + 1 >= pickups.size();'
if (!$gameBody.Contains($originalRule)) { throw 'The collection rule used by this smoke test moved.' }
$editedBody = $gameBody.Replace($originalRule, $changedRule)
[IO.File]::WriteAllText($gameSource, $editedBody, $utf8)
$variantDll = Join-Path $variant 'RavenGame.dll'
& cmake -P $recipeFile *> (Join-Path $scratch 'edited-build.log')
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $variantDll)) {
    throw "The edited game DLL did not build. See $scratch/edited-build.log."
}
$variantHash = (Get-FileHash -LiteralPath $variantDll).Hash
try {
    [IO.File]::WriteAllText($gameSource, "#error intentional smoke build failure`n$editedBody", $utf8)
    & cmake -P $recipeFile *> (Join-Path $scratch 'failed-build.log')
    if ($LASTEXITCODE -eq 0 -or (Get-FileHash -LiteralPath $variantDll).Hash -ne $variantHash) {
        throw 'A failed game build did not preserve the last successful DLL.'
    }
    if (!(Select-String -LiteralPath "$scratch/failed-build.log" -Pattern 'intentional smoke build failure' -Quiet)) {
        throw "The build failed before reaching the intentional compiler error. See $scratch/failed-build.log."
    }
} finally {
    [IO.File]::WriteAllText($gameSource, $editedBody, $utf8)
}

$savedLayers = $env:VK_INSTANCE_LAYERS
$savedSync = $env:VK_LAYER_VALIDATE_SYNC
try {
    $env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    $log = Join-Path $scratch 'smoke.log'
    & "$runtime/Raven3DSmoke.exe" $probe $variantDll *> $log
    $probeStatus = $LASTEXITCODE
    Get-Content -LiteralPath $log
    if ($probeStatus -ne 0 -or (Select-String -LiteralPath $log -Pattern 'VUID-|SYNC-HAZARD|Validation (Error|Warning)' -Quiet)) {
        throw "Windows 3D smoke failed. See $log."
    }
} finally {
    $env:VK_INSTANCE_LAYERS = $savedLayers
    $env:VK_LAYER_VALIDATE_SYNC = $savedSync
}
