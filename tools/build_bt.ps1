<#
.SYNOPSIS
  AIP_DCS BehaviorTree DLL 을 빌드하고 DogFightEnv\Release 로 배치한다.

.DESCRIPTION
  구성은 Debug|x64 다. Release 를 쓰지 마라:
    - vendor 가 배포한 DLL 자체가 Debug 빌드다. JSBSimAIPLib.dll / AIP_BASE*.dll 의 import 를
      보면 MSVCP140D.dll / VCRUNTIME140D.dll / VCRUNTIME140_1D.dll / ucrtbased.dll,
      즉 디버그 CRT 를 요구한다. Release CRT 로 만든 DLL 을 그 호스트에 섞지 마라.
    - 디버그 CRT 는 재배포 불가라 Visual Studio(또는 VS Build Tools 의 MSVC v143) 설치본에만 있다.
      MSVC 를 설치하면 System32 에 자동 배치되므로 PATH 를 손댈 필요는 없다.
      (오히려 VS 의 debug_nonredist / Windows Kits 의 ucrt 폴더를 PATH 앞에 붙이면
       System32 의 ucrtbase.dll 을 가려 엉뚱한 크래시가 난다.)

  빌드 전에 tools\fix_host_project.ps1 을 먼저 돌려라. vcxproj 에 노드가 등록돼 있지 않으면
  **에러 없이** 그 노드가 빠진 DLL 이 나온다.

.PARAMETER HostRoot
  AIP_DCS 폴더(.sln 이 있는 곳). 환경변수 AIP_DCS_ROOT 로도 지정 가능.

.PARAMETER ReleaseDir
  DogFightEnv\Release 경로. -Deploy 일 때만 쓴다.

.PARAMETER TeamName
  배치할 DLL 이름에 쓰인다(AIP_<TeamName>.dll). vendor 의 AIP_BASE*.dll 은 절대 덮어쓰지 않는다.

.EXAMPLE
  .\tools\build_bt.ps1 -HostRoot C:\AIP_LIB\AIP_DCS
  .\tools\build_bt.ps1 -HostRoot C:\AIP_LIB\AIP_DCS -ReleaseDir C:\AIP_LIB\DogFightEnv\Release -Deploy
#>
param(
    [string]$HostRoot   = $(if ($env:AIP_DCS_ROOT) { $env:AIP_DCS_ROOT } else { "C:\AIP_LIB\AIP_DCS" }),
    [string]$ReleaseDir = "C:\AIP_LIB\DogFightEnv\Release",
    [string]$Config     = "Debug",
    [string]$Platform   = "x64",
    [string]$TeamName   = "STIL",
    [switch]$Deploy
)

$ErrorActionPreference = "Stop"

$sln = Join-Path $HostRoot "AIP_DCS.sln"
if (-not (Test-Path $sln)) { throw ".sln 을 찾을 수 없다: $sln" }

# ---- MSBuild 찾기 -------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere 가 없다. VS Build Tools 를 설치하라:`n" +
          "  winget install Microsoft.VisualStudio.2022.BuildTools 또는 https://aka.ms/vs/17/release/vs_BuildTools.exe`n" +
          "  필요 컴포넌트: Microsoft.VisualStudio.Component.VC.Tools.x86.x64, Windows 11 SDK"
}
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                      -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) { throw "MSBuild.exe 를 찾지 못했다. VC 워크로드가 설치돼 있는지 확인하라." }
Write-Host "MSBuild: $msbuild"

# ---- 빌드 ---------------------------------------------------------
$log = Join-Path $PSScriptRoot "msbuild_$Config`_$Platform.log"
Write-Host "빌드: $Config|$Platform"
& $msbuild $sln /t:Build /p:Configuration=$Config /p:Platform=$Platform /m /nologo `
    /v:minimal /flp:"logfile=$log;verbosity=normal" 2>&1 | Tee-Object -Variable out | Out-Host
$code = $LASTEXITCODE

$errors   = @($out | Select-String -Pattern ' error [A-Z]+\d+')
$warnings = @($out | Select-String -Pattern ' warning [A-Z]+\d+')
Write-Host ""
Write-Host ("결과: exit={0}, error={1}, warning={2}" -f $code, $errors.Count, $warnings.Count)
Write-Host "전체 로그: $log"

if ($errors.Count -gt 0) {
    Write-Host "`n--- 첫 컴파일 에러 5개 ---"
    $errors | Select-Object -First 5 | ForEach-Object { "  " + ($_.Line -replace '\s*\[.*\]$','').Trim() }
}
if ($code -ne 0) { exit $code }

# ---- 산출물 -------------------------------------------------------
$outDir = Join-Path (Split-Path $HostRoot -Parent) ("bin\{0}.{1}" -f $Config.ToLower(), $Platform)
$dll = Get-ChildItem $outDir -Filter *.dll -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending
if (-not $dll) { Write-Host "!! DLL 이 없다: $outDir"; exit 1 }
Write-Host "`n산출 DLL:"
$dll | ForEach-Object { "  {0}  {1:N0} bytes  {2}" -f $_.FullName, $_.Length, $_.LastWriteTime }

# ---- 배치 ---------------------------------------------------------
if ($Deploy) {
    if (-not (Test-Path $ReleaseDir)) { throw "ReleaseDir 이 없다: $ReleaseDir" }
    $target  = $dll | Select-Object -First 1
    $teamDll = Join-Path $ReleaseDir "AIP_$TeamName.dll"
    $teamXml = Join-Path $ReleaseDir "Rule.xml"

    foreach ($p in @($teamDll, $teamXml)) {
        if (Test-Path $p) {
            $stamp = (Get-Item $p).LastWriteTime.ToString('yyyyMMddHHmmss')
            Copy-Item $p "$p.bak_$stamp"
            Write-Host "  기존 파일 백업: $p.bak_$stamp"
        }
    }

    Write-Host "`n[deploy] -> $ReleaseDir"
    Copy-Item $target.FullName -Destination $teamDll -Force
    Copy-Item (Join-Path $HostRoot "BehaviorTree\Rule.xml") -Destination $teamXml -Force
    Write-Host "  $($target.Name) -> AIP_$TeamName.dll"
    Write-Host "  Rule.xml"
    Write-Host ""
    Write-Host "  주의: init() 이 createTreeFromFile(`"./Rule.xml`") 로 파일명을 하드코딩한다."
    Write-Host "        Release 루트에는 Rule.xml 이 하나만 존재할 수 있고,"
    Write-Host "        제출용으로 Rule_<team>.xml 로 바꾸려면 init() 의 문자열도 함께 고쳐야 한다."
}
