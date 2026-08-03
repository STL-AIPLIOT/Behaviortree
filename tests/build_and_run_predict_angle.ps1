<#
  PredictManeuver 각도 wrap-around 계약 테스트 빌드 및 실행 (MSVC / Windows).

    .\tests\build_and_run_predict_angle.ps1

  tests/build_and_run_habfm.ps1 과 같은 구조다. g++ 전제의 *.sh 는 Windows 에서
  쓸 수 없으므로 VS Build Tools 의 cl.exe 를 쓴다.

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
# 소스마다 Geometry 를 참조하는 깊이가 다르다.
#   CPPBlackBoard.h        ..\..\..\Geometry   (3단계)
#   BT_Content\Functions.h ..\..\Geometry      (2단계)
# 정션은 <inc>\Geometry 하나만 만들고, include 루트를 깊이별로 두 개 준다.
#   <inc>\a\b\c 기준 3단계 -> <inc>\Geometry
#   <inc>\a\b   기준 2단계 -> <inc>\Geometry
# 정션을 두 곳에 만들면 같은 헤더가 서로 다른 경로로 두 번 열려
# #pragma once 가 듣지 않고 C2374/C2086 재정의가 난다.
$BuildDir = Join-Path $PSScriptRoot "build"
$IncRoot  = Join-Path $BuildDir "inc"
New-Item -ItemType Directory -Force -Path (Join-Path $IncRoot "a\b\c") | Out-Null
foreach ($stale in @("a\Geometry", "a\b\Geometry")) {
    $p = Join-Path $IncRoot $stale
    if (Test-Path $p) { cmd /c "rmdir `"$p`"" 2>$null }
}
$link = Join-Path $IncRoot "Geometry"
if (Test-Path $link) { cmd /c "rmdir `"$link`"" 2>$null }
cmd /c "mklink /J `"$link`" `"$GeometryDir`"" | Out-Null

# 저장소 경로에 한글("바탕 화면")이 들어 있다. 배치 파일은 OEM 코드페이지로
# 읽히므로 한글 경로를 적으면 인용이 깨져 C1083 이 난다.
# -> 배치에는 ASCII 만 남기고, 저장소 안쪽은 전부 $Root 기준 상대경로로 적는다.
#    (cmd 프로세스는 아래에서 $Root 를 작업 디렉터리로 물려받는다.)
$exe = "tests\build\predict_maneuver_angle_test.exe"
$relSources = @(
    "tests\PredictManeuverAngleTest.cpp"
    "BT_Content\Service\PredictManeuver.cpp"
    "BT_Content\Service\PredictManeuverCsvLogger.cpp"
    "BT_Content\BlackBoard\CPPBlackBoard.cpp"
    "BT_Content\Functions.cpp"
    "action_node.cpp"
    "basic_types.cpp"
    "blackboard.cpp"
    "tree_node.cpp"
)
# Geometry 는 저장소 밖(기본 C:\AIP_LIB\...)이라 절대경로를 쓴다. ASCII 라 안전하다.
$absSources = @(
    "$GeometryDir\Vector3.cpp"
    "$GeometryDir\EulerAngle.cpp"
    "$GeometryDir\Quaternion.cpp"
    # EulerAngle/Quaternion 의 변환 생성자가 아래 두 타입을 참조한다(LNK2019 방지).
    "$GeometryDir\AxisAngle.cpp"
    "$GeometryDir\Matrix3.cpp"
    "$GeometryDir\Math.cpp"
)
foreach ($s in $relSources) { if (-not (Test-Path (Join-Path $Root $s))) { throw "소스 없음: $s" } }
foreach ($s in $absSources) { if (-not (Test-Path $s)) { throw "소스 없음: $s" } }
if ($GeometryDir -match '[^\x00-\x7F]') { throw "Geometry 경로에 비ASCII 문자가 있어 배치 빌드가 깨진다: $GeometryDir" }

$clArgs = @(
    "/nologo", "/std:c++17", "/EHsc", "/W0", "/MDd", "/Zi",
    "/D_CRT_SECURE_NO_WARNINGS",
    # 상대경로에는 공백이 없으므로 인용하지 않는다.
    # `"...\"` 형태로 인용하면 끝의 역슬래시가 따옴표를 이스케이프해 버려
    # cl 이 뒤따르는 소스 파일 인자를 전부 삼킨다(D8003).
    "/Itests\build\inc\a\b\c",
    "/Itests\build\inc\a\b",
    "/I.",
    "/Fe:$exe",
    "/Fo:tests\build\",
    # /Fd 를 주지 않으면 vc140.pdb 가 작업 디렉터리(저장소 루트)에 떨어진다.
    "/Fdtests\build\predict_angle.pdb"
) + ($relSources | ForEach-Object { $_ }) + ($absSources | ForEach-Object { "`"$_`"" })

Write-Host "[build] cl.exe 로 컴파일 중..."
$bat = Join-Path $BuildDir "build_predict_angle.bat"
@(
    "@echo off"
    "call `"$vcvars`" >nul 2>&1"
    "cl " + ($clArgs -join " ")
    "exit /b %ERRORLEVEL%"
) | Set-Content -Path $bat -Encoding OEM

Push-Location $Root
try {
    & cmd /c "`"$bat`""
} finally {
    Pop-Location
}
if ($LASTEXITCODE -ne 0) { throw "빌드 실패 (exit $LASTEXITCODE). 배치: $bat" }
$exe = Join-Path $Root $exe
Write-Host "[build] 완료: $exe`n"

# PM_CSV_LOG 가 켜져 있으면 테스트 실행 중에도 CSV 를 쓴다. 테스트는 순수 계산만
# 확인하면 되므로 이 프로세스에 한해 꺼 둔다.
$env:PM_CSV_LOG = ""

& $exe
$code = $LASTEXITCODE
Write-Host "`n[test] exit=$code"
exit $code
