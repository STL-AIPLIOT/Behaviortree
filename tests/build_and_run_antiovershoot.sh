#!/usr/bin/env bash
#
# Task_AntiOvershoot 계약 테스트 빌드 및 실행 스크립트.
#
#   ./tests/build_and_run_antiovershoot.sh
#
# 필요 조건:
# - CPPBlackBoard.h가 저장소 바깥의 Geometry 폴더를 참조하므로
#   저장소와 같은 위치(../Geometry)에 Geometry 헤더가 있어야 한다.
#   다른 경로에 있으면 GEOMETRY_DIR 환경변수로 지정한다.
#
#   예: GEOMETRY_DIR=../tools/harness/Geometry ./tests/build_and_run_antiovershoot.sh
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/.." && pwd)"
GEOMETRY_DIR="${GEOMETRY_DIR:-$(cd "${ROOT}/.." && pwd)/Geometry}"

if [[ ! -d "${GEOMETRY_DIR}" ]]; then
  echo "[error] Geometry 폴더를 찾을 수 없습니다: ${GEOMETRY_DIR}"
  echo "        GEOMETRY_DIR=<경로> ./tests/build_and_run_antiovershoot.sh 형태로 지정하세요."
  exit 1
fi
GEOMETRY_DIR="$(cd "${GEOMETRY_DIR}" && pwd)"

BUILD_DIR="${HERE}/build"
mkdir -p "${BUILD_DIR}"

# Geometry 헤더는 CPPBlackBoard.h에서 ../../../Geometry 상대경로로 참조된다.
# 저장소 옆에 Geometry가 없어도 빌드되도록, 세 단계 아래에 Geometry가 보이는
# include 검색 경로를 빌드 폴더에 만들어 준다.
INC_ROOT="${BUILD_DIR}/inc"
mkdir -p "${INC_ROOT}/a/b/c"
ln -sfn "${GEOMETRY_DIR}" "${INC_ROOT}/Geometry"

SRC="${ROOT}"

# 이 테스트는 노드를 XML이 아니라 코드에서 직접 생성하므로
# bt_factory / xml_parsing / 플러그인 로딩이 필요 없다.
c++ -std=c++17 -O1 -g -w \
  -I "${INC_ROOT}/a/b/c" \
  -o "${BUILD_DIR}/anti_overshoot_test" \
  "${SRC}/tests/AntiOvershootContractTest.cpp" \
  "${SRC}/BT_Content/BlackBoard/CPPBlackBoard.cpp" \
  "${SRC}/BT_Content/Task/Task_AntiOvershoot.cpp" \
  "${SRC}/BT_Content/Task/Task_LeadPursuit.cpp" \
  "${SRC}/action_node.cpp" \
  "${SRC}/basic_types.cpp" \
  "${SRC}/blackboard.cpp" \
  "${SRC}/tree_node.cpp"

echo "[build] 완료: ${BUILD_DIR}/anti_overshoot_test"
"${BUILD_DIR}/anti_overshoot_test"
