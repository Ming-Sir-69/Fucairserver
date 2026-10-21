[CmdletBinding()]
param([ValidateRange(1,32)][int]$Parallel = 2, [switch]$PublicBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'Write-Version.ps1') -PublicBuild:$PublicBuild
. (Join-Path $PSScriptRoot 'Set-BuildEnvironment.ps1')
$compilerRoot = $fucToolchainBin
$build = Join-Path $projectRoot $(if ($PublicBuild) { 'build/miracast-public' } else { 'build/miracast' })
$source = Join-Path $projectRoot 'src/miracast'
$research = Join-Path $projectRoot 'third_party/miracast-reference/research'
$projection = Join-Path $research 'projection'
$generator = Join-Path $research 'cppwinrt-tool/bin/cppwinrt.exe'
$env:TEMP = Join-Path $projectRoot 'temp/compiler'
$env:TMP = $env:TEMP
$env:PATH = $compilerRoot + ';' + $env:PATH
New-Item -ItemType Directory -Path $env:TEMP,$build -Force | Out-Null
if (-not (Test-Path -LiteralPath $generator)) {
    throw 'Pinned Microsoft.Windows.CppWinRT 3.0.260818.1 is missing; see src/miracast/README.md. This build script does not install tools.'
}
$generatorHash = (Get-FileHash -LiteralPath $generator -Algorithm SHA256).Hash.ToLowerInvariant()
if ($generatorHash -ne '90933a0b577bb8e5554d87fc402880aa576dfef5e7b72cc9dba2822e3a6f458e') {
    throw ('Unexpected C++/WinRT 3.0.260818.1 generator SHA-256: ' + $generatorHash)
}
if (-not (Test-Path -LiteralPath (Join-Path $projection 'winrt/Windows.Media.Miracast.h'))) {
    & $generator -input local -output $projection -include Windows.Foundation Windows.Media.Miracast Windows.Media.Playback Windows.ApplicationModel.Core Windows.UI.Core Windows.UI.Composition Windows.System
    if ($LASTEXITCODE -ne 0) { throw 'C++/WinRT projection generation failed' }
}
$compilerPath = $compilerRoot.Replace('\','/')
$arguments = @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    ('-DCMAKE_CXX_COMPILER='+$compilerPath+'/g++.exe'),
    ('-DCMAKE_RC_COMPILER='+$compilerPath+'/windres.exe'),
    ('-DCMAKE_MAKE_PROGRAM='+$compilerPath+'/ninja.exe'),
    ('-DFUCAIRSERVER_CPPWINRT_INCLUDE='+$projection.Replace('\','/')))
& (Join-Path $compilerRoot 'cmake.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Miracast host CMake configuration failed' }
& (Join-Path $compilerRoot 'cmake.exe') --build $build --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw 'Miracast host source build failed' }
Write-Output (Join-Path $build 'Fucairserver.Miracast.exe')
