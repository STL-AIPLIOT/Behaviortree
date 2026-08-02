<#
.SYNOPSIS
  AIP_DCS 호스트 프로젝트를 이 repo 의 BT 트리로 맞춘다.

.DESCRIPTION
  AIP_DCS.vcxproj 는 어느 repo 에도 버전관리되지 않는다. 그래서 새 노드를 추가해도
  각자 로컬에서 VS UI 로 등록해야 하고, 등록을 빠뜨리면 **에러 없이** 그 노드가 빠진 DLL 이
  만들어진다. Rule.xml 은 런타임에 가서야 노드를 못 찾고 실패한다.
  이 스크립트가 그 등록을 자동화하고, vendor vcxproj 의 경로 결함도 함께 고친다.

  고치는 것:
    1. BT_Content 의 .cpp/.h 중 ClCompile/ClInclude 에 없는 것을 전부 추가
    2. AdditionalIncludeDirectories 의 절대경로(D:\PropertySheets 등)를
       $(SolutionDir)..\PropertySheets 로 바꾸고 %(AdditionalIncludeDirectories) 상속 복원
    3. ..\..\PropertySheets\*.props import 를 ..\PropertySheets\ 로 교정
       (Debug 구성만 원래 맞았고 Release 는 해석 자체가 안 됐다)
    4. _CRT_SECURE_NO_WARNINGS 추가 — /sdl 이 C4996(getenv)을 error 로 승격시켜
       PredictManeuverCsvLogger.cpp / LeadPursuitTelemetry.cpp 가 빌드를 깬다

  멱등하다. 여러 번 돌려도 중복 추가하지 않는다. vcxproj 는 최초 1회 .bak 으로 백업한다.

.PARAMETER HostRoot
  AIP_DCS 폴더 경로(AIP_DCS.vcxproj 가 있는 곳). 환경변수 AIP_DCS_ROOT 로도 지정 가능.

.PARAMETER SyncSources
  이 repo 의 BT_Content/, CPPBehaviorTree.cpp/.h, Rule.xml 을 호스트로 복사한다.

.PARAMETER Apply
  실제로 반영한다. 없으면 dry-run.

.EXAMPLE
  .\tools\fix_host_project.ps1 -HostRoot C:\AIP_LIB\AIP_DCS
  .\tools\fix_host_project.ps1 -HostRoot C:\AIP_LIB\AIP_DCS -SyncSources -Apply
#>
param(
    [string]$HostRoot = $(if ($env:AIP_DCS_ROOT) { $env:AIP_DCS_ROOT } else { "C:\AIP_LIB\AIP_DCS" }),
    [switch]$SyncSources,
    [switch]$Apply
)

$ErrorActionPreference = "Stop"

# 이 스크립트는 <repo>\tools\ 에 있다. 팀 repo 루트는 그 부모.
$TeamRepo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$vcxPath  = Join-Path $HostRoot "AIP_DCS.vcxproj"

if (-not (Test-Path $vcxPath)) {
    throw "AIP_DCS.vcxproj 를 찾을 수 없다: $vcxPath`n  -HostRoot 로 경로를 주거나 AIP_DCS_ROOT 를 설정하라."
}
Write-Host "팀 repo : $TeamRepo"
Write-Host "호스트  : $HostRoot"
Write-Host ""

# ---------------------------------------------------------------- 0) 소스 동기화
if ($SyncSources) {
    $btDst = Join-Path $HostRoot "BehaviorTree"
    if (-not (Test-Path $btDst)) { throw "호스트에 BehaviorTree 폴더가 없다: $btDst" }
    Write-Host "[sync] -> $btDst"
    foreach ($item in @("BT_Content", "CPPBehaviorTree.cpp", "CPPBehaviorTree.h", "Rule.xml")) {
        $src = Join-Path $TeamRepo $item
        if (-not (Test-Path $src)) { Write-Host "  ! 없음, 건너뜀: $item"; continue }
        if ($Apply) { Copy-Item $src -Destination $btDst -Recurse -Force; Write-Host "  copied: $item" }
        else        { Write-Host "  (dry-run) would copy: $item" }
    }
    Write-Host ""
}

# ---------------------------------------------------------------- 1) vcxproj 로드
[xml]$xml = Get-Content $vcxPath -Raw
$ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
$ns.AddNamespace("m", "http://schemas.microsoft.com/developer/msbuild/2003")
$msb = $ns.LookupNamespace("m")

$clCompileNodes = $xml.SelectNodes("//m:ClCompile[@Include]", $ns)
$clIncludeNodes = $xml.SelectNodes("//m:ClInclude[@Include]", $ns)
if ($clCompileNodes.Count -eq 0) { throw "vcxproj 에 ClCompile 항목이 하나도 없다. 파일이 맞는지 확인하라." }

$listedC = @($clCompileNodes | ForEach-Object { $_.GetAttribute("Include") })
$listedH = @($clIncludeNodes | ForEach-Object { $_.GetAttribute("Include") })
$groupC  = $clCompileNodes[0].ParentNode
$groupH  = if ($clIncludeNodes.Count -gt 0) { $clIncludeNodes[0].ParentNode } else { $groupC }

function Get-RelPaths([string]$ext) {
    $base   = Join-Path $TeamRepo "BT_Content"
    $prefix = "$base\"
    Get-ChildItem $base -Recurse -Filter "*.$ext" |
        ForEach-Object { "BehaviorTree\BT_Content\" + $_.FullName.Substring($prefix.Length) }
}

$addedC = 0; $addedH = 0
foreach ($rel in Get-RelPaths "cpp") {
    if ($listedC -contains $rel) { continue }
    $n = $xml.CreateElement("ClCompile", $msb); $n.SetAttribute("Include", $rel) | Out-Null
    [void]$groupC.AppendChild($n); Write-Host "  + ClCompile $rel"; $addedC++
}
foreach ($rel in Get-RelPaths "h") {
    if ($listedH -contains $rel) { continue }
    $n = $xml.CreateElement("ClInclude", $msb); $n.SetAttribute("Include", $rel) | Out-Null
    [void]$groupH.AppendChild($n); Write-Host "  + ClInclude $rel"; $addedH++
}

# ---------------------------------------------------------------- 2) include 경로
$fixedInc = 0
foreach ($node in $xml.SelectNodes("//m:AdditionalIncludeDirectories", $ns)) {
    if ($node.InnerText -match '^[A-Za-z]:\\PropertySheets$') {
        $node.InnerText = '$(SolutionDir)..\PropertySheets;%(AdditionalIncludeDirectories)'
        Write-Host "  ~ AdditionalIncludeDirectories: 절대경로 -> SolutionDir 상대 + 상속 복원"
        $fixedInc++
    }
}

# ---------------------------------------------------------------- 3) props import
$fixedImp = 0
foreach ($node in $xml.SelectNodes("//m:Import[@Project]", $ns)) {
    $p = $node.GetAttribute("Project")
    if ($p -like '..\..\PropertySheets\*') {
        $new = $p -replace '^\.\.\\\.\.\\', '..\'
        $node.SetAttribute("Project", $new) | Out-Null
        Write-Host "  ~ Import: $p -> $new"
        $fixedImp++
    }
}

# ---------------------------------------------------------------- 4) _CRT_SECURE_NO_WARNINGS
$fixedDef = 0
foreach ($pd in $xml.SelectNodes("//m:ClCompile/m:PreprocessorDefinitions", $ns)) {
    if ($pd.InnerText -notmatch '_CRT_SECURE_NO_WARNINGS') {
        $pd.InnerText = "_CRT_SECURE_NO_WARNINGS;" + $pd.InnerText
        $fixedDef++
    }
}
if ($fixedDef -gt 0) { Write-Host "  ~ _CRT_SECURE_NO_WARNINGS 를 $fixedDef 개 구성에 추가 (getenv C4996 -> error 방지)" }

# ---------------------------------------------------------------- 결과
$total = $addedC + $addedH + $fixedInc + $fixedImp + $fixedDef
Write-Host ""
Write-Host ("요약: ClCompile +{0}, ClInclude +{1}, include경로 {2}, Import {3}, 전처리기 {4}" -f `
            $addedC, $addedH, $fixedInc, $fixedImp, $fixedDef)

if (-not $Apply) { Write-Host "DRY-RUN - 파일을 쓰지 않았다. -Apply 로 반영."; exit 0 }
if ($total -eq 0) { Write-Host "변경 없음 (이미 반영된 상태)."; exit 0 }

$backup = "$vcxPath.bak"
if (-not (Test-Path $backup)) { Copy-Item $vcxPath $backup; Write-Host "백업: $backup" }
$xml.Save($vcxPath)
Write-Host "반영 완료: $vcxPath"
