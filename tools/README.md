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
- **`init()` 의 XML 파일명은 환경변수 `BT_RULE_XML` 로 열려 있다** (2026-08-08 추가).
  없으면 종전과 같은 `./Rule.xml` 이라 기본 동작은 바뀌지 않는다.
  `export` 를 늘리지 않고 환경변수로 연 이유는 `native_bt.py` 가 바인딩하는 export 가
  6종으로 고정이고, 추가하면 수정 금지 영역인 호스트를 건드려야 하기 때문이다.

- **벤더 `AIP_BASE_target.dll` 은 `./Rule_forTraining.xml` 을 읽는다.** (2026-08-08 정정)

  > 이 문서에는 한동안 "벤더 DLL 은 팀 `Rule.xml` 이 깔린 상태에서 못 쓴다" 고 적혀
  > 있었다. **틀린 진단이었다.** 벤더 DLL 바이너리에 들어 있는 XML 경로 문자열은
  > `./Rule_forTraining.xml` **하나뿐**이고 `./Rule.xml` 은 아예 없다. 즉 팀 XML 과
  > 충돌한 것이 아니라 **자기가 찾는 파일이 배포 트리에 없어서** 죽은 것이다.
  > 증상(`OSError: [WinError -529697949]`, `0xe06d7363`)이 같아 오진하기 쉽다.

  그 파일은 vendor 드롭에 그 이름으로 들어 있지 않다. 내용은 `C:\AIP_LIB\Rule.xml`
  (1,104 B, 커스텀 8종)과 같으므로 그 이름으로 복사해 두면 된다:

  ```powershell
  copy C:\AIP_LIB\Rule.xml C:\AIP_LIB\DogFightEnv\Release\Rule_forTraining.xml
  ```

  배치하면 **두 DLL 을 동시에 쓸 수 있다** — 벤더는 `Rule_forTraining.xml`, 팀은
  `Rule.xml`(또는 `BT_RULE_XML` 로 지정한 것)을 각자 읽는다. 실측 확인:

  ```
  [OK] 벤더 AIP_BASE_target.dll  (./Rule_forTraining.xml)
  [OK] 팀   AIP_STIL.dll         (./Rule.xml)   nodes=45
  ```

  다만 **로드된다는 것과 쓸 만하다는 것은 다르다.**

- **`AIP_BASE_target.dll` 은 상대로 쓰지 마라 — 기동하지 않는다** (2026-08-12 실측).

  `Rule_forTraining.xml` 의 행동 노드는 `Task_Empty` **둘뿐**이다. 거리로 분기해 놓고
  양쪽 다 빈 태스크로 끝난다:

  ```xml
  <Fallback>
    <Sequence><DECO_DistanceCheck UpDown="Greater" Distance="2000"/><Task_Empty/></Sequence>
    <Sequence><DECO_DistanceCheck UpDown="Less"    Distance="2000"/><Task_Empty/></Sequence>
  </Fallback>
  ```

  나머지 6종은 전부 정보 갱신 서비스라 **VP 를 쓰는 노드가 하나도 없다.** 60초 교전
  실측에서 `|roll|` 중앙 **1.7°**, 초기 침로로 직진해 N 좌표가 5,990 → **−19,448 m**.
  파일명 `forTraining` 그대로 학습용 정지 표적이다. **결함이 아니라 설계다.**

  노드를 추가해 살릴 수도 없다 — 등록은 `Factory.registerNodeType<Action::X>("X")`
  라는 C++ 코드인데 소스 없는 벤더 바이너리다. XML 만으로는 등록되지 않는다.

- **기동하는 상대가 필요하면 팀 DLL 사본을 쓴다.**

  ```powershell
  Copy-Item C:\AIP_LIB\DogFightEnv\Release\AIP_STIL.dll `
            C:\AIP_LIB\DogFightEnv\Release\AIP_STIL_target.dll
  ```

  파일명이 **달라야** 한다. `ctypes.cdll.LoadLibrary` 는 같은 경로면 같은 핸들을
  돌려주므로, 양쪽 provider 에 `AIP_STIL.dll` 을 주면 BT 레지스트리를 공유해 한
  인스턴스가 된다. 사본이면 핸들이 분리된다(실측 확인).

  ```powershell
  python run_local_dogfight.py --ownship-backend bt --ownship-bt-dll AIP_STIL.dll `
      --target-backend bt --target-bt-dll AIP_STIL_target.dll --max-engage-time 60 --save-log
  ```

  20판 실측: 양쪽 다 기동(`|roll|` 중앙 76.0° / 54.4°), **피해 조건 충족 프레임 972,
  20/20 판에서 상호 유효타**. 근거는 `Release/analysis/EXP/EXP-007_selfplay_opponent.md`.

  > **DLL 을 새로 배포하면 사본도 같이 갱신하라.** 해시가 갈리면 대칭이 깨져 비교가
  > 성립하지 않는다.

- `/sdl` 이 C4996 을 error 로 올려서 `getenv` 를 쓰는 `PredictManeuverCsvLogger.cpp` /
  `LeadPursuitTelemetry.cpp` 가 빌드를 깬다. 스크립트가 `_CRT_SECURE_NO_WARNINGS` 를 넣어 해결한다.

## 검증된 결과 (2026-08-02)

`Debug|x64`, **error 0 / warning 342**, `AIP_DCS.dll` 2,696,704 bytes.
배치 후 `CreateBehaviorTree(1,1)` 실호출로 `Rule.xml` 파싱과 커스텀 노드 26종 등록까지 확인했고,
`native_bt.py` 가 바인딩하는 export 6개(`CreateBehaviorTree` / `ChangeData` / `Step` / `GetVP` /
`Reset` / `RemoveBT`)가 모두 존재한다.

## `bt_node_gate.py` — 배치 전에 XML/DLL 조합을 검사한다

`createTreeFromFile()` 은 등록되지 않은 노드 태그를 만나면 예외를 던진다. 그 예외가
ctypes 경계를 넘어오면 파이썬 쪽에는 `OSError: [WinError -529697949]`(0xe06d7363) 만
남는다. **DLL 경로 문제처럼 보이지만 원인은 XML 이다.**

```powershell
python tools\bt_node_gate.py Rule.xml AIP_STIL.dll          # 0 = 통과, 1 = 부족분 있음
python tools\bt_node_gate.py Rule.xml AIP_BASE.dll --json out.json
```

**DLL 을 로드하지 않는다.** 로드하면 그 자리에서 죽는 게 이 문제의 본질이라, 파일을
바이트로 읽어 ASCII/UTF-16LE 문자열만 훑는 정적 분석이다.

세 조합 실측 (2026-08-06):

| XML | DLL | 커스텀 | 확인 | 부족 | exit |
|---|---|---:|---:|---:|---:|
| 팀 `Rule.xml` | 벤더 `AIP_BASE.dll` | 27 | 7 | **20** | 1 |
| 팀 `Rule.xml` | 팀 `AIP_STIL.dll` | 27 | 27 | 0 | 0 |
| 벤더 `Rule.xml` | 벤더 `AIP_BASE.dll` | 8 | 8 | 0 | 0 |

첫 줄이 실제로 죽는 조합이다. 부족한 20종은 팀이 추가한 BFM 로직 전부
(`SetBFMMode_*` 4, `Task_*` 13, `CanRetry`, `DECO_CounterAttackCheck`,
`EnergyCompare`, `PredictManeuver`).

`tools/bt_node_gate_sample.json` 이 두 번째 줄(PASS)의 `--json` 출력 샘플이다.

### 결과를 오독하지 말 것

`registerNodeType<T>("Name")` 의 인자는 컴파일되면 그냥 문자열 리터럴이라, 바이너리
안에서 다른 용도의 같은 문자열과 구별되지 않는다. 이 도구는 **"그 이름의 문자열이
있다" 까지만** 말하고 "등록돼 있다" 고는 말하지 않는다. 이름이 겹쳐도 구현이 다를 수
있다(벤더 DLL 에도 `Task_Pure` 가 있지만 팀 구현과 같다는 보장은 없다).

판정은 OK / CHECK(짧거나 일반적인 이름 — 오탐 가능) / MISSING 세 등급이고,
**MISSING 만 종료 코드에 반영한다.** CHECK 로 빌드를 막으면 오탐 때문에 도구를 꺼
버리게 되고, 그러면 진짜 부족분도 놓친다.

## 세 번째 함정 — XML 만 고치면 Release 에 안 간다 (2026-08-08)

`Rule.xml` 은 두 단계를 거쳐야 실행에 반영되는데 **두 단계의 트리거가 다르다.**

| 구간 | 수행 주체 | 실행 조건 |
|---|---|---|
| (1) 팀 트리 -> (2) 빌드 트리 | `build_bt.ps1` 의 `Sync-BehaviorTreeSources` | **빌드할 때마다** (기본 켜짐) |
| (2) 빌드 트리 -> (3) Release 루트 | `build_bt.ps1:216` 의 `-Deploy` 블록 | **`-Deploy` 를 줄 때만** |

**XML 만 고치면 빌드할 이유가 없다.** 그래서 `build_bt.ps1` 을 아예 안 돌리거나
`-Deploy` 없이 돌리게 되고, (3) 은 옛 XML 그대로 남는다. DLL 은 그 옛 XML 을 읽는다.

이 상태는 **조용하다.** 옛 XML 도 유효한 XML 이라 파싱은 성공하고 노드 구성만 의도와
달라진다. 빌드 에러도 런타임 에러도 없다. "XML 을 고쳤는데 동작이 그대로" 로 나타난다.

### 대응 — `sync_rule_xml.ps1`

```powershell
.	ools\sync_rule_xml.ps1            # 확인만. 어디가 낡았는지 보여준다 (exit 1 = 낡음)
.	ools\sync_rule_xml.ps1 -Apply     # (1) -> (2) -> (3) 전파
```

빌드하지 않는다. **C++ 을 안 고쳤으면 이걸 쓰고, 고쳤으면 `build_bt.ps1 -Deploy` 를 쓴다.**

하는 일:

1. 세 위치의 SHA256·요소 수·수정시각을 나란히 보여준다
2. `-Apply` 면 팀 트리 것으로 (2)(3) 을 덮는다. 덮기 전에 `.bak_<타임스탬프>` 로 백업한다
3. **복사 후 해시를 다시 재서** 실제로 반영됐는지 확인한다 (복사했다고 반영된 게 아니다)
4. 배포한 XML 과 Release 의 팀 DLL 을 `bt_node_gate.py` 로 대조한다 —
   XML 만 새것이고 DLL 이 낡았으면 `... is not a registered node` 로 죽으므로 여기서 잡는다

검증: 팀 XML 에 주석 한 줄을 넣어 드리프트를 만든 뒤 확인 모드가 `exit 1` 로 잡아냈고,
`-Apply` 후 세 곳 해시 일치 + 노드 게이트 통과를 확인했다.

> 백업 파일이 쌓인다. Release 루트의 `Rule.xml.bak_*` 은 배포 대상이 아니므로
> 제출 전에 정리하라. **`Rule.xml` 본체는 이름을 바꾸거나 옮기지 마라** —
> `init()` 이 `createTreeFromFile("./Rule.xml")` 로 하드코딩한다.
