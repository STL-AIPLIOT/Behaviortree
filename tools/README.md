# tools

BT DLL 을 빌드해서 `DogFightEnv\Release` 에 놓기까지의 절차를 스크립트로 굳혀둔 것.

## 왜 필요한가

`AIP_DCS.vcxproj` 는 **어느 repo 에도 들어 있지 않다.** vendor 드롭에만 있고 각자 로컬에서만 바뀐다.
그런데 이 프로젝트는 소스 파일을 개별 열거하는 방식이라, 새 노드를 만들고 vcxproj 에 등록하지 않으면

> **빌드는 에러 없이 성공하고, 그 노드가 빠진 DLL 이 나온다.**

`Rule.xml` 은 런타임에 가서야 노드를 찾지 못하고 실패한다. 2026-08-02 기준 vendor vcxproj 에는
팀 `BT_Content` 소스 39개 중 12개만 등록돼 있었다 — `SetBFMMode_*`, `Task_AntiOvershoot`,
`Task_LeadPursuit`, `DECO_CanRetry` 등이 전부 빠져 있었다.

**새 노드를 추가했으면 반드시 `fix_host_project.ps1` 을 다시 돌려라.**

## 두 번째 함정 — 호스트 소스가 낡아도 빌드는 성공한다 (2026-08-04)

`vcxproj` 가 컴파일하는 것은 팀 저장소가 아니라 **`<HostRoot>\BehaviorTree`** 다.
팀 트리를 고쳐도 그쪽으로 복사하지 않으면 **옛 소스가 조용히 빌드된다.**

실제 사고: `xml_parsing.cpp` 의 수정 — 자식 노드 연결을 부모 `name()` 문자열 비교에서
타입(`dynamic_cast<ControlNode*>`) 판정으로 바꾼 것 — 이 호스트로 가지 않았다.
`Rule.xml` 의 제어 노드에는 전부 `name="..."` 별칭이 붙어 있어(`MainCombatSequence`,
`SelectAndExecuteBFM`, `HABFM_Action` …) 옛 코드에서는 **자식이 단 하나도 연결되지 않았다.**

증상은 이랬다.

```
[tree] MainCombatSequence [Control, children=0]     ← 노드 45개는 만들어졌지만 전부 고아
BFM=NONE 1468/1468 tick,  VP=(0,0,0),  Distance=0
SetBFMMode 로그 0줄,  PredictManeuver CSV 미생성
```

빌드 에러도, 런타임 에러도, 로그도 없었다. `--target-backend bt` 는 사실상
`target_mode: fixed` 와 동일하게 동작했고 그 상태로 스파링 데이터가 쌓였다.

원인은 두 겹이었다.

1. `fix_host_project.ps1 -SyncSources` 의 복사 목록에 저장소 루트 소스가 없었다
   (`BT_Content`, `CPPBehaviorTree.*`, `Rule.xml` 뿐).
2. 팀 트리의 `xml_parsing.cpp` 자체가 컴파일되지 않았다 — `dynamic_cast` 수정을 넣으면서
   `decorator_parent` 선언 줄이 함께 지워져 `error C2065` 가 났다.
   그래서 호스트에는 빌드되는 옛 파일이 남아 있었다.

**대응: `build_bt.ps1` 이 매 빌드마다 팀 트리 전체를 해시 비교로 동기화한다.**
낡아 있던 파일은 이름까지 출력한다.

```
[sync] ...\Behaviortree  ->  C:\AIP_LIB\AIP_DCS\BehaviorTree
[sync] 낡아 있던 파일 1개를 갱신했다:
         xml_parsing.cpp
[sync] 호스트에만 있는 소스 2개 (팀 트리에 없음, 그대로 둔다):
         BT_Content\Task\Task_Empty.cpp
```

`-NoSync` 로 끌 수 있지만 쓰지 마라. 호스트에만 있는 소스는 vcxproj 가 컴파일할 수 있으므로
삭제하지 않고 목록만 보여준다 — 필요 없는 파일이면 vcxproj 에서 빼라.

> BT 를 고쳤는데 동작이 안 바뀌면, 먼저 `[sync]` 출력을 보라.

## 사용

```powershell
# 1) 호스트 프로젝트를 이 repo 기준으로 맞춘다 (dry-run 이 기본)
.\tools\fix_host_project.ps1 -HostRoot C:\AIP_LIB\AIP_DCS
.\tools\fix_host_project.ps1 -HostRoot C:\AIP_LIB\AIP_DCS -SyncSources -Apply

# 2) 빌드하고 배치한다
.\tools\build_bt.ps1 -HostRoot C:\AIP_LIB\AIP_DCS
.\tools\build_bt.ps1 -HostRoot C:\AIP_LIB\AIP_DCS -ReleaseDir C:\AIP_LIB\DogFightEnv\Release -Deploy
```

경로는 `AIP_DCS_ROOT` 환경변수로도 줄 수 있다. `fix_host_project.ps1` 은 멱등이고
vcxproj 를 최초 1회 `.bak` 으로 백업한다.

## 알아둘 것

- **구성은 `Debug|x64`.** vendor 가 배포한 DLL 이 Debug 빌드라 디버그 CRT
  (`MSVCP140D` / `VCRUNTIME140D` / `VCRUNTIME140_1D` / `ucrtbased`)를 요구한다.
  Release CRT 로 만든 DLL 을 섞지 마라. vcxproj 의 Release 구성은 props import 경로도 깨져 있었다
  (`..\..\PropertySheets` → 해석 불가). `fix_host_project.ps1` 이 고치지만, 그래도 Debug 로 빌드하라.
- **디버그 CRT 는 재배포 불가**라 Visual Studio / VS Build Tools 의 MSVC v143 설치본에만 있다.
  설치하면 `System32` 에 자동으로 들어가므로 `PATH` 를 건드릴 필요 없다.
  오히려 `debug_nonredist` 나 Windows Kits 의 `ucrt` 폴더를 `PATH` **앞**에 붙이면
  `System32` 의 `ucrtbase.dll` 을 가려서 전혀 상관없어 보이는 크래시가 난다.
- **DLL 이름은 `AIP_<TeamName>.dll`** 로 배치된다. Release 루트의 `AIP_BASE.dll` /
  `AIP_BASE_target.dll` 은 vendor 파일이라 절대 덮어쓰지 않는다.
- **`init()` 이 `createTreeFromFile("./Rule.xml")` 로 파일명을 하드코딩**한다. 따라서 Release 루트에
  `Rule.xml` 은 하나만 둘 수 있고, 제출용으로 `Rule_<team>.xml` 로 바꾸려면 그 문자열도 함께 고쳐야 한다.
  DLL 과 XML 은 항상 한 세트로 옮긴다.
- **그 결과 벤더 `AIP_BASE_target.dll` 은 팀 `Rule.xml` 이 깔린 상태에서 못 쓴다** (2026-08-04 실측).
  두 DLL 이 같은 `./Rule.xml` 을 읽는데 팀 XML(98요소)에는 벤더 빌드가 등록하지 않은 노드가 들어 있어,
  `CreateBehaviorTree` 가 C++ 예외를 던지고 ctypes 경계를 넘어 `OSError: [WinError -529697949]`
  (`0xe06d7363`) 로 올라온다. **DLL 이나 경로 문제로 보이지만 원인은 XML 이다.**
  - 증상 위치: `native_bt.py:125` → `FighterSim.py:339` → `single_agent_env.py:165`
  - BT 상대 학습(`target_mode: behavior_tree`)은 `target_behavior_dll: AIP_STIL.dll` 로 돌린다.
  - 벤더 원본 `Rule.xml`(1104 B, 2026-05-20)은 `C:\AIP_LIB\Rule.xml` 에 남아 있다.
    벤더 상대가 필요하면 루트 XML 을 그것으로 되돌려야 하고, 그동안 팀 DLL 은 쓸 수 없다.
    **둘을 동시에 쓸 방법은 파일명 하드코딩을 고치지 않는 한 없다.**
- `/sdl` 이 C4996 을 error 로 올려서 `getenv` 를 쓰는 `PredictManeuverCsvLogger.cpp` /
  `LeadPursuitTelemetry.cpp` 가 빌드를 깬다. 스크립트가 `_CRT_SECURE_NO_WARNINGS` 를 넣어 해결한다.

## 검증된 결과 (2026-08-02)

`Debug|x64`, **error 0 / warning 342**, `AIP_DCS.dll` 2,696,704 bytes.
배치 후 `CreateBehaviorTree(1,1)` 실호출로 `Rule.xml` 파싱과 커스텀 노드 26종 등록까지 확인했고,
`native_bt.py` 가 바인딩하는 export 6개(`CreateBehaviorTree` / `ChangeData` / `Step` / `GetVP` /
`Reset` / `RemoveBT`)가 모두 존재한다.
