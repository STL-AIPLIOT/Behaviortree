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

  2026-08-04: 빌드가 되지 않던 문제 세 가지를 고쳤다(tests/build_and_run_predict_angle.ps1 과 동일).
    1) 저장소 경로에 한글("바탕 화면")이 있는데 배치 파일은 OEM 코드페이지로 읽힌다.
       한글 경로를 배치에 적으면 인용이 깨져 C1083 이 났다.
       -> 저장소 안쪽은 전부 상대경로로 적고, cmd 에 $Root 를 작업 디렉터리로 물려준다.
    2) `/Fo:"...\"` 처럼 끝 역슬래시를 인용하면 따옴표가 이스케이프돼
       cl 이 뒤따르는 소스 인자를 전부 삼켰다(D8003). -> 인용하지 않는다.
    3) EulerAngle/Quaternion 의 변환 생성자가 AxisAngle/Matrix3 를 참조해 LNK2019 가 났다.
       -> 두 소스를 추가했다.
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
if ($GeometryDir -match '[^\x00-\x7F]') {
    throw "Geometry 경로에 비ASCII 문자가 있어 배치 빌드가 깨진다: $GeometryDir"
}

# ---- cl.exe 찾기 ----------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere 없음. VS Build Tools 를 설치하라." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "MSVC v143 툴셋을 찾지 못했다." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat 없음: $vcvars" }

# ---- include 경로 준비 ----------------------------------------------
# 소스마다 Geometry 참조 깊이가 다르다.
#   CPPBlackBoard.h        ..\..\..\Geometry   (3단계)
#   BT_Content\Functions.h ..\..\Geometry      (2단계)
# 정션은 <inc>\Geometry 하나만 만들고 include 루트를 깊이별로 두 개 준다.
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

$exe = "tests\build\habfm_circlemode_test.exe"

# 저장소 안쪽: 상대경로(공백·한글 없음). Geometry: 저장소 밖이라 절대경로.
$relSources = @(
    "tests\HabfmCircleModeContractTest.cpp"
    "BT_Content\BlackBoard\CPPBlackBoard.cpp"
    "BT_Content\Task\Task_OneCircleAttack.cpp"
    "BT_Content\Task\Task_TwoCircleAttack.cpp"
    "BT_Content\Functions.cpp"
    "action_node.cpp"
    "basic_types.cpp"
    "blackboard.cpp"
    "tree_node.cpp"
)
$absSources = @(
    "$GeometryDir\Vector3.cpp"
    "$GeometryDir\EulerAngle.cpp"
    "$GeometryDir\Quaternion.cpp"
    "$GeometryDir\AxisAngle.cpp"
    "$GeometryDir\Matrix3.cpp"
    "$GeometryDir\Math.cpp"
)
foreach ($s in $relSources) { if (-not (Test-Path (Join-Path $Root $s))) { throw "소스 없음: $s" } }
foreach ($s in $absSources) { if (-not (Test-Path $s)) { throw "소스 없음: $s" } }

$clArgs = @(
    "/nologo", "/std:c++17", "/EHsc", "/W0", "/MDd", "/Zi",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/Itests\build\inc\a\b\c",
    "/Itests\build\inc\a\b",
    "/I.",
    "/Fe:$exe",
    "/Fo:tests\build\",
    # /Fd 를 주지 않으면 vc140.pdb 가 저장소 루트에 떨어진다.
    "/Fdtests\build\habfm.pdb"
) + $relSources + ($absSources | ForEach-Object { "`"$_`"" })

Write-Host "[build] cl.exe 로 컴파일 중..."
$bat = Join-Path $BuildDir "build_habfm.bat"
@(
    "@echo off"
    "call `"$vcvars`" >nul 2>&1"
    "cl " + ($clArgs -join " ")
    "exit /b %ERRORLEVEL%"
) | Set-Content -Path $bat -Encoding OEM

Push-Location $Root
try { & cmd /c "`"$bat`"" } finally { Pop-Location }
if ($LASTEXITCODE -ne 0) { throw "빌드 실패 (exit $LASTEXITCODE). 배치: $bat" }
$exe = Join-Path $Root $exe
Write-Host "[build] 완료: $exe`n"

& $exe
$code = $LASTEXITCODE
Write-Host "`n[test] exit=$code"
exit $code
