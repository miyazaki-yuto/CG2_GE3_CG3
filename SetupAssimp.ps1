$ErrorActionPreference = 'Stop'

$assimpSource = Join-Path $PSScriptRoot 'externals\assimp'
$assimpBuild = Join-Path $assimpSource 'build-v143'
$assimpProject = Join-Path $assimpBuild 'code\assimp.vcxproj'

if (-not (Test-Path -LiteralPath (Join-Path $assimpSource 'CMakeLists.txt'))) {
    & git -C $PSScriptRoot submodule update --init --depth 1 externals/assimp
    if ($LASTEXITCODE -ne 0) {
        throw 'Assimp submodule initialization failed.'
    }
}

$cmakeCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)

$cachePath = Join-Path $assimpBuild 'CMakeCache.txt'
$cachedGenerator = $null
if ([System.IO.File]::Exists($cachePath)) {
    $generatorEntry = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=(.+)$' |
        Select-Object -First 1
    if ($generatorEntry) {
        $cachedGenerator = $generatorEntry.Matches[0].Groups[1].Value
    }
}

$cmakePath = if ($cachedGenerator -eq 'Visual Studio 18 2026') {
    $cmakeCandidates[1]
} elseif ($cachedGenerator -eq 'Visual Studio 17 2022') {
    $cmakeCandidates[0]
} else {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
}
if (-not $cmakePath -or -not (Test-Path -LiteralPath $cmakePath)) {
    $cmakePath = $cmakeCandidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
}
if (-not $cmakePath) {
    throw 'CMake was not found. Install the C++ CMake tools component in Visual Studio.'
}

$commonArguments = @(
    '-S', $assimpSource,
    '-B', $assimpBuild,
    '-DBUILD_SHARED_LIBS=OFF',
    '-DASSIMP_BUILD_TESTS=OFF',
    '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF',
    '-DASSIMP_BUILD_SAMPLES=OFF',
    '-DASSIMP_INSTALL=OFF',
    '-DASSIMP_WARNINGS_AS_ERRORS=OFF',
    '-DASSIMP_NO_EXPORT=ON',
    '-DASSIMP_BUILD_ZLIB=ON',
    '-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF',
    '-DASSIMP_BUILD_OBJ_IMPORTER=ON',
    '-DASSIMP_BUILD_GLTF_IMPORTER=ON',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Debug>>:DLL>'
)

if ($cachedGenerator) {
    & $cmakePath @commonArguments
} else {
    $generator = if ($cmakePath -like '*\2022\*') {
        'Visual Studio 17 2022'
    } else {
        'Visual Studio 18 2026'
    }
    & $cmakePath @commonArguments -G $generator -A x64 -T v143
}

if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $assimpProject)) {
    throw 'Assimp project generation failed.'
}

Write-Host "Assimp OBJ/glTF importer project is ready: $assimpProject"
