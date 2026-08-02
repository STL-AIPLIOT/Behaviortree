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
- `/sdl` 이 C4996 을 error 로 올려서 `getenv` 를 쓰는 `PredictManeuverCsvLogger.cpp` /
  `LeadPursuitTelemetry.cpp` 가 빌드를 깬다. 스크립트가 `_CRT_SECURE_NO_WARNINGS` 를 넣어 해결한다.

## 검증된 결과 (2026-08-02)

`Debug|x64`, **error 0 / warning 342**, `AIP_DCS.dll` 2,696,704 bytes.
배치 후 `CreateBehaviorTree(1,1)` 실호출로 `Rule.xml` 파싱과 커스텀 노드 26종 등록까지 확인했고,
`native_bt.py` 가 바인딩하는 export 6개(`CreateBehaviorTree` / `ChangeData` / `Step` / `GetVP` /
`Reset` / `RemoveBT`)가 모두 존재한다.
