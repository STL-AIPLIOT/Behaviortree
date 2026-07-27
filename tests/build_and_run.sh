#!/usr/bin/env bash
#
# CanRetry 데코레이터 상태 전이 테스트 빌드 및 실행 스크립트.
#
#   ./tests/build_and_run.sh
#
# 필요 조건:
# - CPPBlackBoard.h가 저장소 바깥의 Geometry 폴더를 참조하므로
#   저장소와 같은 위치(../Geometry)에 Geometry 헤더가 있어야 한다.
#   다른 경로에 있으면 GEOMETRY_DIR 환경변수로 지정한다.
#
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/.." && pwd)"
GEOMETRY_DIR="${GEOMETRY_DIR:-$(cd "${ROOT}/.." && pwd)/Geometry}"

if [[ ! -d "${GEOMETRY_DIR}" ]]; then
  echo "[error] Geometry 폴더를 찾을 수 없습니다: ${GEOMETRY_DIR}"
  echo "        GEOMETRY_DIR=<경로> ./tests/build_and_run.sh 형태로 지정하세요."
  exit 1
fi

BUILD_DIR="${HERE}/build"
mkdir -p "${BUILD_DIR}"

# 저장소 안에는 POSIX용 SharedLibrary 구현이 없다(플러그인 로딩은 Windows 전용).
# 테스트에서는 플러그인을 사용하지 않으므로 링크만 통과하도록 스텁을 만든다.
cat > "${BUILD_DIR}/shared_library_stub.cpp" <<'STUB'
#include "../../behaviortree_cpp_v3/utils/shared_library.h"
#include "../../behaviortree_cpp_v3/exceptions.h"
namespace BT
{
SharedLibrary::SharedLibrary() { _handle = nullptr; }
void SharedLibrary::load(const std::string&, int) { throw RuntimeError("plugins are not supported in tests"); }
void* SharedLibrary::findSymbol(const std::string&) { return nullptr; }
std::string SharedLibrary::prefix() { return "lib"; }
std::string SharedLibrary::suffix() { return ".so"; }
}
STUB

# Geometry 헤더는 CPPBlackBoard.h에서 ../../../Geometry 상대경로로 참조된다.
# 저장소 옆에 Geometry가 없어도 빌드되도록, 세 단계 아래에 Geometry가 보이는
# include 검색 경로를 빌드 폴더에 만들어 준다.
INC_ROOT="${BUILD_DIR}/inc"
mkdir -p "${INC_ROOT}/a/b/c"
ln -sfn "${GEOMETRY_DIR}" "${INC_ROOT}/Geometry"

SRC="${ROOT}"

c++ -std=c++17 -O1 -g -w \
  -I "${INC_ROOT}/a/b/c" \
  -o "${BUILD_DIR}/can_retry_test" \
  "${SRC}/tests/CanRetryTest.cpp" \
  "${BUILD_DIR}/shared_library_stub.cpp" \
  "${SRC}/BT_Content/BlackBoard/CPPBlackBoard.cpp" \
  "${SRC}/BT_Content/Decorator/DECO_CanRetry.cpp" \
  "${SRC}/action_node.cpp" \
  "${SRC}/basic_types.cpp" \
  "${SRC}/behavior_tree.cpp" \
  "${SRC}/blackboard.cpp" \
  "${SRC}/bt_factory.cpp" \
  "${SRC}/condition_node.cpp" \
  "${SRC}/control_node.cpp" \
  "${SRC}/decorator_node.cpp" \
  "${SRC}/shared_library.cpp" \
  "${SRC}/tinyxml2.cpp" \
  "${SRC}/tree_node.cpp" \
  "${SRC}/xml_parsing.cpp" \
  "${SRC}"/controls/*.cpp \
  "${SRC}"/decorators/*.cpp

echo "[build] 완료: ${BUILD_DIR}/can_retry_test"
"${BUILD_DIR}/can_retry_test"
