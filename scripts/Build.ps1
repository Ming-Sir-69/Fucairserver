[CmdletBinding()]
param([ValidateRange(1,32)][int]$Parallel = 4, [switch]$PublicBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'Write-Version.ps1') -PublicBuild:$PublicBuild
. (Join-Path $PSScriptRoot 'Set-BuildEnvironment.ps1')
$source = Join-Path $projectRoot 'third_party/AirPlay-Windows'
$build = Join-Path $projectRoot $(if ($PublicBuild) { 'build/native-public' } else { 'build/native' })
$compilerRoot = $fucToolchainBin.Replace('\','/')
$cmakeRoot = $projectRoot.Replace('\','/')
$arguments = @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    ('-DCMAKE_C_COMPILER='+$compilerRoot+'/gcc.exe'),
    ('-DCMAKE_CXX_COMPILER='+$compilerRoot+'/g++.exe'),
    ('-DCMAKE_RC_COMPILER='+$compilerRoot+'/windres.exe'),
    ('-DCMAKE_MAKE_PROGRAM='+$compilerRoot+'/ninja.exe'),
    ('-DFETCHCONTENT_SOURCE_DIR_IMGUI='+$cmakeRoot+'/third_party/imgui'),
    ('-DFUCAIRSERVER_PROJECT_ROOT='+$cmakeRoot),'-DUSE_GSTREAMER_HLS=OFF','-DBUILD_NETWORK_TESTS=ON')
& (Join-Path $compilerRoot 'cmake.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Native receiver CMake configuration failed' }
& (Join-Path $compilerRoot 'cmake.exe') --build $build --parallel $Parallel
if ($LASTEXITCODE -ne 0) { throw 'Native receiver source build failed' }
Write-Output (Join-Path $build 'Fucairserver.exe')
