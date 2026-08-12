# -*- coding: utf-8 -*-
"""Rule.xml 이 참조하는 커스텀 노드가 DLL 에 등록돼 있는지 **정적으로** 검사한다.

왜 필요한가
-----------
BehaviorTree.CPP v3 는 `createTreeFromFile()` 에서 등록되지 않은 노드 태그를 만나면
예외를 던진다. 그 예외가 ctypes 경계를 넘어오면 파이썬 쪽에는

    OSError: [WinError -529697949]      # 0xe06d7363 = C++ exception

만 남는다. **DLL 경로 문제처럼 보이지만 원인은 XML 이고**, 로그가 없어 추적에 오래 걸린다
(2026-08-04 실측: 벤더 `AIP_BASE_target.dll` + 팀 `Rule.xml` 조합에서 재현).

이 도구는 DLL 을 **로드하지 않고** 바이너리에서 문자열만 훑어 그 조합이 성립하는지
미리 알려준다. 로드하면 그 자리에서 죽기 때문에 정적 분석이어야 한다.

한계 — 이걸 모르면 결과를 오독한다
-----------------------------------
`registerNodeType<T>("Name")` 의 인자는 컴파일되면 그냥 문자열 리터럴이라, 바이너리
안에서 **다른 용도의 같은 문자열과 구별되지 않는다.** 그래서 이 도구는

  * "DLL 에 그 이름의 문자열이 있다"  까지만 말한다.
  * "그러므로 등록돼 있다" 고는 말하지 않는다.

이름이 있어도 구현이 다를 수 있고(동명이인), 없어도 매크로/템플릿으로 조립된 이름이면
놓칠 수 있다. 판정은 세 등급으로 나눈다:

    OK      XML 에 있고 DLL 에서도 찾음            -> 통과로 세되 동작 보장은 아님
    MISSING XML 에 있는데 DLL 에서 못 찾음          -> 거의 확실히 죽는다
    CHECK   찾긴 했으나 근거가 약함(짧은 이름 등)   -> 사람이 봐야 한다

**MISSING 만 종료 코드에 반영한다.** CHECK 로 빌드를 막으면 오탐 때문에 도구를 끄게 된다.

사용
----
    python tools/bt_node_gate.py Rule.xml AIP_STIL.dll
    python tools/bt_node_gate.py Rule.xml AIP_BASE.dll --json out.json

종료 코드: 0 = 부족분 없음 / 1 = 부족분 있음 / 2 = 입력 오류
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# ── BehaviorTree.CPP v3 내장 노드 ─────────────────────────────────────────────
# 팩토리가 기본 등록하므로 DLL 문자열에 없어도 정상이다.
# 출처: BehaviorTree.CPP v3 `BehaviorTreeFactory::BehaviorTreeFactory()`.
# 팀 트리가 vendor 사본을 쓰므로 목록을 파일에서 읽지 않고 여기 고정한다.
BUILTIN_NODES = frozenset({
    # 문서 구조 (노드가 아님)
    "root", "BehaviorTree", "TreeNodesModel", "include",
    # Control
    "Sequence", "SequenceStar", "ReactiveSequence",
    "Fallback", "FallbackStar", "ReactiveFallback",
    "Parallel", "IfThenElse", "WhileDoElse", "Switch2", "Switch3", "Switch4",
    "Switch5", "Switch6", "ManualSelector",
    # Decorator
    "Inverter", "ForceSuccess", "ForceFailure", "Repeat", "RetryUntilSuccesful",
    "RetryUntilSuccessful", "Timeout", "Delay", "KeepRunningUntilFailure",
    "BlackboardCheckInt", "BlackboardCheckDouble", "BlackboardCheckString",
    "SubTree", "SubTreePlus",
    # Action
    "AlwaysSuccess", "AlwaysFailure", "SetBlackboard", "Script", "SubTreeNode",
})

# 이 길이 미만이거나 흔한 영어 단어면 문자열 스캔 오탐 가능성이 높다.
_SHORT_NAME_LEN = 6
_GENERIC_NAMES = frozenset({"Update", "Check", "Target", "Task", "Action", "State"})

# 인쇄 가능 ASCII 시퀀스. 노드 이름은 식별자라 이 범위를 벗어나지 않는다.
_ASCII_RE = re.compile(rb"[\x20-\x7e]{3,96}")
# UTF-16LE: ASCII 문자마다 NUL 이 뒤따른다.
_UTF16_RE = re.compile(rb"(?:[\x20-\x7e]\x00){3,96}")


def collect_xml_nodes(path: Path) -> tuple[set[str], set[str], int]:
    """Rule.xml 의 태그를 (커스텀, 내장, 총 요소수) 로 나눈다."""
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise SystemExit(f"[error] XML 파싱 실패: {path}\n  {exc}")
    tags = [el.tag for el in root.iter()]
    custom = {t for t in tags if t not in BUILTIN_NODES}
    builtin = {t for t in tags if t in BUILTIN_NODES}
    return custom, builtin, len(tags)


def extract_strings(path: Path) -> set[str]:
    """DLL 바이너리에서 ASCII / UTF-16LE 문자열을 모은다.

    **로드하지 않는다.** 등록 안 된 노드를 참조하는 DLL 을 로드하면 그 자리에서
    죽기 때문에, 이 도구는 파일을 바이트로만 읽는다.
    """
    try:
        blob = path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"[error] DLL 을 읽지 못함: {path}\n  {exc}")

    out: set[str] = set()
    for m in _ASCII_RE.finditer(blob):
        out.add(m.group().decode("ascii"))
    for m in _UTF16_RE.finditer(blob):
        out.add(m.group().decode("utf-16-le", "ignore"))
    return out


def confidence(name: str, blob_strings: set[str]) -> str:
    """찾은 이름의 확신도. 'ok' 또는 'check'."""
    if name not in blob_strings:
        return "missing"
    # 짧거나 일반적인 단어는 노드 이름이 아닌 다른 문자열일 수 있다.
    if len(name) < _SHORT_NAME_LEN or name in _GENERIC_NAMES:
        return "check"
    return "ok"


def analyse(xml_path: Path, dll_path: Path) -> dict:
    custom, builtin, total = collect_xml_nodes(xml_path)
    strings = extract_strings(dll_path)

    found, uncertain, missing = [], [], []
    for name in sorted(custom):
        verdict = confidence(name, strings)
        (found if verdict == "ok" else uncertain if verdict == "check" else missing).append(name)

    return {
        "xml": str(xml_path),
        "dll": str(dll_path),
        "dll_size_bytes": dll_path.stat().st_size,
        "xml_elements_total": total,
        "builtin_used": sorted(builtin),
        "custom_required": sorted(custom),
        "found": found,
        "needs_review": uncertain,
        "missing": missing,
        "counts": {
            "custom_required": len(custom),
            "found": len(found),
            "needs_review": len(uncertain),
            "missing": len(missing),
        },
        "verdict": "PASS" if not missing else "FAIL",
        "caveat": ("문자열 존재만 확인한 정적 분석이다. 이름이 있어도 registerNodeType "
                   "인자라는 보장은 없고, 구현이 다를 수도 있다."),
    }


def report(res: dict) -> None:
    c = res["counts"]
    print(f"XML : {res['xml']}  (요소 {res['xml_elements_total']}개)")
    print(f"DLL : {res['dll']}  ({res['dll_size_bytes'] / 1024:.0f} KB)")
    print(f"\n커스텀 노드 {c['custom_required']}종 중 "
          f"확인 {c['found']} / 검토필요 {c['needs_review']} / **부족 {c['missing']}**\n")

    if res["found"]:
        print("  [OK] DLL 에서 이름을 찾음")
        for n in res["found"]:
            print(f"        {n}")
    if res["needs_review"]:
        print("\n  [확인 필요] 찾긴 했으나 이름이 짧거나 일반적이라 오탐일 수 있다")
        for n in res["needs_review"]:
            print(f"        {n}")
    if res["missing"]:
        print("\n  [부족] XML 에는 있는데 DLL 에서 찾지 못함")
        print("         이 조합으로 createTreeFromFile() 을 부르면 예외로 죽는다.")
        print("         ctypes 를 거치면 OSError: [WinError -529697949] 로만 보인다.")
        for n in res["missing"]:
            print(f"        {n}")

    print(f"\n판정: {res['verdict']}")
    print(f"주의: {res['caveat']}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Rule.xml 의 커스텀 노드가 DLL 에 있는지 정적 검사 (DLL 을 로드하지 않는다)")
    ap.add_argument("xml", type=Path, help="Rule.xml 경로")
    ap.add_argument("dll", type=Path, help="검사할 DLL 경로")
    ap.add_argument("--json", type=Path, default=None, help="결과를 JSON 으로 저장")
    ap.add_argument("--quiet", action="store_true", help="사람용 출력 생략")
    args = ap.parse_args(argv)

    for p in (args.xml, args.dll):
        if not p.is_file():
            print(f"[error] 파일이 없다: {p}", file=sys.stderr)
            return 2

    res = analyse(args.xml, args.dll)
    if not args.quiet:
        report(res)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(res, ensure_ascii=False, indent=2), encoding="utf-8")
        if not args.quiet:
            print(f"\nJSON: {args.json}")

    # needs_review 는 종료 코드에 반영하지 않는다. 오탐으로 빌드를 막으면
    # 도구를 꺼 버리게 되고, 그러면 진짜 부족분도 놓친다.
    return 1 if res["missing"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
