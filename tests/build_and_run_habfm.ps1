<#
  HABFM 1C/2C 진입 계약 테스트 빌드 및 실행 (MSVC / Windows).

    .\tests\build_and_run_habfm.ps1

  기존 tests/build_and_run*.sh 는 g++ 전제라 Windows 에서 쓸 수 없다.
  이 스크립트는 VS Build Tools 의 cl.exe 를 쓴다.

  필요 조건:
    - VS Build Tools (MSVC v143). vswhere 로 자동 탐색한다.
    - CPPBlackBoard.h 가 ..\..\..\Geometry 상대경로로 Geometry 헤더를 참조하므로
      Geometry 폴더 경로가 필요하다. -GeometryDir 로 지정하거나
      AIP_DCS_ROOT\Geometry 를 기본으로 쓴다.
#>
param(
    [string]$GeometryDir = $(if ($env:AIP_DCS_ROOT) { Join-Path $env:AIP_DCS_ROOT "Geometry" } else { "C:\AIP_LIB\AIP_DCS\Geometry" })
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not (Test-Path $GeometryDir)) {
    throw "Geometry 폴더를 찾을 수 없다: $GeometryDir`n  -GeometryDir <경로> 로 지정하라."
}
$GeometryDir = (Resolve-Path $GeometryDir).Path

# ---- cl.exe 찾기 ----------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere 없음. VS Build Tools 를 설치하라." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "MSVC v143 툴셋을 찾지 못했다." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat 없음: $vcvars" }

# ---- include 경로 준비 ----------------------------------------------
# CPPBlackBoard.h 는 ..\..\..\Geometry 로 참조한다.
# a\b\c 를 include 루트로 주면 a\b\c\..\..\..\Geometry = <inc>\Geometry 로 해석된다.
$BuildDir = Join-Path $PSScriptRoot "build"
$IncRoot  = Join-Path $BuildDir "inc"
New-Item -ItemType Directory -Force -Path (Join-Path $IncRoot "a\b\c") | Out-Null
$link = Join-Path $IncRoot "Geometry"
if (Test-Path $link) { cmd /c "rmdir `"$link`"" 2>$null }
cmd /c "mklink /J `"$link`" `"$GeometryDir`"" | Out-Null

$exe = Join-Path $BuildDir "habfm_circlemode_test.exe"
$sources = @(
    "$PSScriptRoot\HabfmCircleModeContractTest.cpp"
    "$Root\BT_Content\BlackBoard\CPPBlackBoard.cpp"
    "$Root\BT_Content\Task\Task_OneCircleAttack.cpp"
    "$Root\BT_Content\Task\Task_TwoCircleAttack.cpp"
    "$Root\BT_Content\Functions.cpp"
    "$Root\action_node.cpp"
    "$Root\basic_types.cpp"
    "$Root\blackboard.cpp"
    "$Root\tree_node.cpp"
    "$GeometryDir\Vector3.cpp"
    "$GeometryDir\EulerAngle.cpp"
    "$GeometryDir\Math.cpp"
)
foreach ($s in $sources) { if (-not (Test-Path $s)) { throw "소스 없음: $s" } }

$clArgs = @(
    "/nologo", "/std:c++17", "/EHsc", "/W0", "/MDd", "/Zi",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/I`"$IncRoot\a\b\c`"",
    "/I`"$Root`"",
    "/Fe:`"$exe`"",
    "/Fo:`"$BuildDir\`""
) + ($sources | ForEach-Object { "`"$_`"" })

Write-Host "[build] cl.exe 로 컴파일 중..."
# 경로에 공백/한글이 있어 `cmd /c "<긴 명령>"` 은 인용이 깨진다. 배치 파일 경유.
$bat = Join-Path $BuildDir "build_habfm.bat"
@(
    "@echo off"
    "call `"$vcvars`" >nul 2>&1"
    "cl " + ($clArgs -join " ")
    "exit /b %ERRORLEVEL%"
) | Set-Content -Path $bat -Encoding OEM
& cmd /c "`"$bat`""
if ($LASTEXITCODE -ne 0) { throw "빌드 실패 (exit $LASTEXITCODE). 배치: $bat" }
Write-Host "[build] 완료: $exe`n"

& $exe
$code = $LASTEXITCODE
Write-Host "`n[test] exit=$code"
exit $code
