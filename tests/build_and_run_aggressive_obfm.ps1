<#
  Task_AggressiveOBFM 계약 테스트 빌드 및 실행 (MSVC / Windows).

    .\tests\build_and_run_aggressive_obfm.ps1

  구조는 tests\build_and_run_habfm.ps1 과 같다. 그 스크립트의 주석에 적힌
  세 가지 함정(한글 경로 / /Fo 끝 역슬래시 인용 / Geometry 링크 소스 누락)이
  여기에도 그대로 적용된다.

  ATK 분기: 기본 Geometry 경로가 C:\AIP_LIB\AIP_DCS_ATK\Geometry 다.
#>
param(
    [string]$GeometryDir = $(if ($env:AIP_DCS_ROOT) { Join-Path $env:AIP_DCS_ROOT "Geometry" } else { "C:\AIP_LIB\AIP_DCS_ATK\Geometry" })
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
# 정션은 <inc>\Geometry 하나만 만들고 include 루트를 깊이별로 준다.
# 두 곳에 만들면 같은 헤더가 다른 경로로 두 번 열려 #pragma once 가 듣지 않는다.
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

$exe = "tests\build\aggressive_obfm_test.exe"

$relSources = @(
    "tests\AggressiveOBFMContractTest.cpp"
    "BT_Content\BlackBoard\CPPBlackBoard.cpp"
    "BT_Content\Task\Task_AggressiveOBFM.cpp"
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

# /utf-8: 소스가 BOM 없는 UTF-8 이다. 없으면 CP949 로 읽혀 한글 주석이 개행을
# 삼키고 코드가 주석에 먹힌다(AIP_DCS_ATK\AIP_DCS.vcxproj 의 같은 옵션 주석 참조).
$clArgs = @(
    "/nologo", "/std:c++17", "/EHsc", "/W0", "/MDd", "/Zi", "/utf-8",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/Itests\build\inc\a\b\c",
    "/Itests\build\inc\a\b",
    "/I.",
    "/Fe:$exe",
    "/Fo:tests\build\",
    "/Fdtests\build\aggressive_obfm.pdb"
) + $relSources + ($absSources | ForEach-Object { "`"$_`"" })

Write-Host "[build] cl.exe 로 컴파일 중..."
$bat = Join-Path $BuildDir "build_aggressive_obfm.bat"
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
