<#
.SYNOPSIS
    Rule.xml 을 팀 트리 -> 빌드 트리 -> Release 루트로 전파한다. 빌드하지 않는다.

.DESCRIPTION
    Rule.xml 은 두 단계를 거쳐야 실행에 반영되는데, 두 단계의 트리거가 서로 다르다.

        (1) Behaviortree\Rule.xml            팀 트리 · 편집하는 곳
              |  build_bt.ps1 의 Sync-BehaviorTreeSources  <- 빌드할 때마다
              v
        (2) <HostRoot>\BehaviorTree\Rule.xml  빌드 트리
              |  build_bt.ps1 의 -Deploy 블록              <- -Deploy 를 줄 때만
              v
        (3) <ReleaseDir>\Rule.xml             런타임 · DLL 이 실제로 읽는 것

    **XML 만 고치면 빌드할 이유가 없다.** 그래서 build_bt.ps1 을 아예 안 돌리거나
    -Deploy 없이 돌리게 되고, (3) 은 옛 XML 그대로 남는다. DLL 은 그 옛 XML 을 읽는다.

    이 상태는 조용하다 — 옛 XML 도 유효한 XML 이라 파싱은 성공하고 노드 구성만
    의도와 달라진다. 빌드 에러도 런타임 에러도 없다.

    이 스크립트는 그 구멍만 메운다. C++ 을 안 고쳤으면 이걸 쓰고, 고쳤으면
    build_bt.ps1 -Deploy 를 쓴다.

.EXAMPLE
    # 확인만 (기본). 어디가 어긋났는지 보여주고 아무것도 바꾸지 않는다.
    .\tools\sync_rule_xml.ps1

    # 실제로 전파
    .\tools\sync_rule_xml.ps1 -Apply

.NOTES
    Release 루트의 Rule.xml 은 이름을 바꾸거나 옮기지 않는다.
    init() 이 createTreeFromFile("./Rule.xml") 로 파일명을 하드코딩하므로
    그 루트에는 Rule.xml 이 하나만 존재할 수 있다.
#>
[CmdletBinding()]
param(
    [string]$HostRoot   = $(if ($env:AIP_DCS_ROOT) { $env:AIP_DCS_ROOT } else { "C:\AIP_LIB\AIP_DCS" }),
    [string]$ReleaseDir = "C:\AIP_LIB\DogFightEnv\Release",
    # 팀 저장소 루트. 비우면 이 스크립트의 부모(= Behaviortree\)를 쓴다.
    # param 블록 기본값에서 $PSScriptRoot 는 호출 방식에 따라 비어 있을 수 있어
    # 여기서 계산하지 않고 본문에서 채운다.
    [string]$TeamRoot   = "",
    # 실제로 복사한다. 주지 않으면 확인만 한다.
    [switch]$Apply,
    # 배치 후 노드 게이트를 돌리지 않는다.
    [switch]$SkipGate
)

$ErrorActionPreference = "Stop"

$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $TeamRoot) { $TeamRoot = Split-Path -Parent $scriptDir }

function Get-Sha8([string]$path) {
    if (-not (Test-Path $path)) { return "(없음)" }
    (Get-FileHash $path -Algorithm SHA256).Hash.Substring(0, 8).ToLower()
}

function Get-NodeCount([string]$path) {
    if (-not (Test-Path $path)) { return -1 }
    try { ([xml](Get-Content $path -Raw)).SelectNodes("//*").Count }
    catch { -1 }
}

$team    = Join-Path $TeamRoot   "Rule.xml"
$build   = Join-Path $HostRoot   "BehaviorTree\Rule.xml"
$runtime = Join-Path $ReleaseDir "Rule.xml"

if (-not (Test-Path $team)) { throw "팀 Rule.xml 이 없다: $team" }

Write-Host "Rule.xml 전파 경로"
Write-Host ""
$rows = @(
    @{ n = "(1) 팀 트리   "; p = $team    }
    @{ n = "(2) 빌드 트리 "; p = $build   }
    @{ n = "(3) Release   "; p = $runtime }
)
foreach ($r in $rows) {
    $h = Get-Sha8 $r.p
    $c = Get-NodeCount $r.p
    $t = if (Test-Path $r.p) { (Get-Item $r.p).LastWriteTime.ToString('MM-dd HH:mm:ss') } else { "-" }
    "  {0} {1,9}  요소 {2,3}  {3}" -f $r.n, $h, $c, $t | Write-Host
}

$hTeam    = Get-Sha8 $team
$hBuild   = Get-Sha8 $build
$hRuntime = Get-Sha8 $runtime

$stale = @()
if ($hBuild   -ne $hTeam) { $stale += "(2) 빌드 트리" }
if ($hRuntime -ne $hTeam) { $stale += "(3) Release 루트" }

Write-Host ""
if ($stale.Count -eq 0) {
    Write-Host "  세 곳 모두 일치. 할 일 없음." -ForegroundColor Green
} else {
    Write-Host ("  낡음: " + ($stale -join ", ")) -ForegroundColor Yellow
    if ($hRuntime -ne $hTeam) {
        Write-Host "  -> DLL 이 지금 읽는 것은 팀 트리의 XML 이 아니다." -ForegroundColor Yellow
        Write-Host "     파싱은 성공하므로 에러 없이 노드 구성만 달라진다." -ForegroundColor Yellow
    }
}

if (-not $Apply) {
    if ($stale.Count -gt 0) {
        Write-Host ""
        Write-Host "  실제로 맞추려면 -Apply 를 줘라."
    }
    exit $(if ($stale.Count -gt 0) { 1 } else { 0 })
}

# ---- 전파 ----------------------------------------------------------------
if ($stale.Count -eq 0) { exit 0 }

Write-Host ""
foreach ($dst in @($build, $runtime)) {
    $parent = Split-Path -Parent $dst
    if (-not (Test-Path $parent)) {
        Write-Warning "대상 폴더가 없어 건너뛴다: $parent"
        continue
    }
    # 덮어쓰기 전에 백업한다. Release 루트의 XML 은 되돌릴 수단이 이것뿐이다.
    if (Test-Path $dst) {
        $stamp = (Get-Item $dst).LastWriteTime.ToString('yyyyMMddHHmmss')
        Copy-Item $dst "$dst.bak_$stamp" -Force
        Write-Host "  백업 $dst.bak_$stamp"
    }
    Copy-Item $team -Destination $dst -Force
    Write-Host "  복사 -> $dst"
}

# ---- 반영 확인 -----------------------------------------------------------
# 복사했다고 반영된 것이 아니다. 해시로 확인한다.
Write-Host ""
$ok = $true
foreach ($r in @(@{ n = "(2) 빌드 트리"; p = $build }, @{ n = "(3) Release  "; p = $runtime })) {
    $h = Get-Sha8 $r.p
    if ($h -eq $hTeam) { "  [OK]   {0}  {1}" -f $r.n, $h | Write-Host }
    else { "  [FAIL] {0}  {1} (기대 {2})" -f $r.n, $h, $hTeam | Write-Host -ForegroundColor Red; $ok = $false }
}
if (-not $ok) { throw "복사 후에도 해시가 다르다. 파일 잠김이나 권한을 확인하라." }

# ---- 노드 정합 -----------------------------------------------------------
# XML 만 새것이고 DLL 이 낡으면 "... is not a registered node" 로 죽는다.
# 배포한 XML 과 실제 DLL 을 대조해 그 조합을 미리 잡는다.
if (-not $SkipGate) {
    $gate = Join-Path $scriptDir "bt_node_gate.py"
    $dlls = @(Get-ChildItem -Path $ReleaseDir -Filter "AIP_*.dll" -File -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -notlike "AIP_BASE*" })
    if ((Test-Path $gate) -and $dlls.Count -gt 0) {
        Write-Host ""
        Write-Host "노드 정합 검사"
        foreach ($d in $dlls) {
            & python $gate $runtime $d.FullName --quiet
            $code = $LASTEXITCODE
            if ($code -eq 0) { "  [OK]   {0}" -f $d.Name | Write-Host }
            else {
                "  [FAIL] {0} — XML 이 참조하는 노드가 DLL 에 없다. DLL 이 낡았다." -f $d.Name |
                    Write-Host -ForegroundColor Red
                Write-Host "         .\tools\build_bt.ps1 -Deploy 로 DLL 까지 다시 배치하라."
                Write-Host "         자세한 부족분: python tools\bt_node_gate.py `"$runtime`" `"$($d.FullName)`""
                exit 1
            }
        }
    } else {
        Write-Warning "노드 게이트를 건너뛴다 (bt_node_gate.py 또는 팀 DLL 없음)."
    }
}

Write-Host ""
Write-Host "완료. DLL 이 읽는 XML 이 팀 트리와 같아졌다." -ForegroundColor Green
exit 0
