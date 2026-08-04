// HABFM 1C/2C 진입 계약 테스트
//
// 빌드/실행: tests/build_and_run_habfm.ps1  (MSVC / Windows)
//
// 외부 테스트 프레임워크 없이 실패 개수를 세는 방식.
// (tests/AntiOvershootContractTest.cpp, tests/CanRetryTest.cpp 와 동일한 방식)
//
// 이 테스트가 고정하는 사실
// -------------------------
// 과거 1C/2C 는 각 Task 가 자기 기준으로 myV / tgV 를 비교해 진입을 결정했고,
// myV == tgV 에서 두 Task 가 동시에 FAILURE 가 되어 HABFM 분기 전체가
// 무행동이 되는 구멍이 있었다 (Task_OneCircleAttack.h:17 참조).
//
// 현재 구현은 진입 기준을 SetBFMMode_HABFM 이 선회율로 정한
// BB->HABFM_CircleMode 로 옮겼다:
//     Task_OneCircleAttack.cpp:32  HABFM_CircleMode != ONE_CIRCLE -> FAILURE
//     Task_TwoCircleAttack.cpp:29  HABFM_CircleMode != TWO_CIRCLE -> FAILURE
//     CPPBlackBoard.cpp:38         초기값 CIRCLE_NONE
//
// 따라서 속도가 같아도 1C/2C 진입 여부는 영향을 받지 않아야 한다.
// 다만 CIRCLE_NONE 이면 두 Task 가 모두 양보하므로, Rule.xml 의
// HABFM_Action Fallback 마지막에 있는 reposition(Task_MinimizeAngleOff)이
// 반드시 있어야 무행동이 되지 않는다. 그 존재는 이 테스트 범위 밖이며
// Rule.xml 정적 검사로 확인한다.

#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>

#include "../BT_Content/Task/Task_OneCircleAttack.h"
#include "../BT_Content/Task/Task_TwoCircleAttack.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& label)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "  [FAIL] " << label << "\n";
    } else {
        std::cout << "  [OK]   " << label << "\n";
    }
}

const char* StatusName(BT::NodeStatus s)
{
    switch (s) {
        case BT::NodeStatus::SUCCESS: return "SUCCESS";
        case BT::NodeStatus::FAILURE: return "FAILURE";
        case BT::NodeStatus::RUNNING: return "RUNNING";
        default:                      return "IDLE";
    }
}

const char* CircleName(HABFM_Circle c)
{
    switch (c) {
        case ONE_CIRCLE: return "1C";
        case TWO_CIRCLE: return "2C";
        default:         return "NONE";
    }
}

// 교전 중 상태를 흉내낸 블랙보드. 위치/자세는 헤드온에 가깝게 둔다.
void SetupBlackBoard(CPPBlackBoard& bb, float myV, float tgV, HABFM_Circle mode)
{
    bb.RunningTime      = 10.0;
    bb.DeltaSecond      = 0.02;
    bb.MySpeed_MS       = myV;
    bb.TargetSpeed_MS   = tgV;
    bb.HABFM_CircleMode = mode;
    bb.BFM              = HABFM;
    bb.EnemyInSight     = true;
    bb.Distance         = 1200.0f;

    // 주의: 원본 필드명이 TargetLocaion_Cartesian (Locaion) 이다. 오타지만 그대로 쓴다.
    bb.MyLocation_Cartesian    = Vector3(0.0f, 0.0f, 7000.0f);
    bb.TargetLocaion_Cartesian = Vector3(1200.0f, 0.0f, 7000.0f);
    bb.MyRotation_EDegree      = EulerAngle(0.0f, 0.0f, 0.0f);
    bb.TargetRotation_EDegree  = EulerAngle(180.0f, 0.0f, 0.0f);

    bb.VP_Cartesian = Vector3(0.0f, 0.0f, 0.0f);
    bb.Throttle     = 0.0f;
}

// VP 가 갱신됐는지 = 실제 action 이 실행됐는지의 대리 지표
bool VpWritten(const CPPBlackBoard& bb)
{
    return !(bb.VP_Cartesian.X == 0.0f &&
             bb.VP_Cartesian.Y == 0.0f &&
             bb.VP_Cartesian.Z == 0.0f);
}

struct Outcome {
    BT::NodeStatus one;
    BT::NodeStatus two;
    bool           one_acted;
    bool           two_acted;
};

Outcome RunPair(float myV, float tgV, HABFM_Circle mode)
{
    Outcome out{};

    {
        CPPBlackBoard bb;
        SetupBlackBoard(bb, myV, tgV, mode);
        BT::NodeConfiguration cfg;
        cfg.blackboard = BT::Blackboard::create();
        cfg.blackboard->set("BB", &bb);
        cfg.input_ports["BB"] = "{BB}";
        Action::Task_OneCircleAttack node("Task_OneCircleAttack", cfg);
        out.one = node.executeTick();
        out.one_acted = VpWritten(bb);
    }
    {
        CPPBlackBoard bb;
        SetupBlackBoard(bb, myV, tgV, mode);
        BT::NodeConfiguration cfg;
        cfg.blackboard = BT::Blackboard::create();
        cfg.blackboard->set("BB", &bb);
        cfg.input_ports["BB"] = "{BB}";
        Action::Task_TwoCircleAttack node("Task_TwoCircleAttack", cfg);
        out.two = node.executeTick();
        out.two_acted = VpWritten(bb);
    }
    return out;
}

}  // namespace

int main()
{
    std::cout << "=== HABFM 1C/2C 진입 계약 테스트 ===\n\n";

    const float cases[][2] = {
        {300.0f,     250.0f},
        {250.0f,     300.0f},
        {300.0f,     300.0f},
        {300.0001f,  300.0f},
        {299.9999f,  300.0f},
    };

    std::cout << "[1] 속도 차이는 1C/2C 진입에 영향을 주지 않는다\n";
    std::cout << "    (진입 기준은 HABFM_CircleMode 이며 속도가 아니다)\n\n";

    for (const auto& c : cases) {
        const float myV = c[0], tgV = c[1];
        std::cout << "  --- myV=" << std::fixed << std::setprecision(4) << myV
                  << "  tgV=" << tgV << " ---\n";

        for (HABFM_Circle mode : {CIRCLE_NONE, ONE_CIRCLE, TWO_CIRCLE}) {
            Outcome o = RunPair(myV, tgV, mode);
            std::cout << "    CircleMode=" << std::setw(4) << CircleName(mode)
                      << "  1C=" << std::setw(7) << StatusName(o.one)
                      << " (action " << (o.one_acted ? "실행" : "없음") << ")"
                      << "  2C=" << std::setw(7) << StatusName(o.two)
                      << " (action " << (o.two_acted ? "실행" : "없음") << ")\n";

            if (mode == CIRCLE_NONE) {
                Check(o.one == BT::NodeStatus::FAILURE && o.two == BT::NodeStatus::FAILURE,
                      "CIRCLE_NONE 이면 1C/2C 둘 다 FAILURE (Rule.xml reposition 이 받아야 함)");
            } else if (mode == ONE_CIRCLE) {
                Check(o.one != BT::NodeStatus::FAILURE,
                      "ONE_CIRCLE 이면 1C 가 FAILURE 가 아니다");
                Check(o.two == BT::NodeStatus::FAILURE,
                      "ONE_CIRCLE 이면 2C 는 FAILURE");
            } else {
                Check(o.two != BT::NodeStatus::FAILURE,
                      "TWO_CIRCLE 이면 2C 가 FAILURE 가 아니다");
                Check(o.one == BT::NodeStatus::FAILURE,
                      "TWO_CIRCLE 이면 1C 는 FAILURE");
            }
        }
        std::cout << "\n";
    }

    std::cout << "[2] 같은 CircleMode 라면 속도가 달라도 결과가 같다 (속도 무관성)\n";
    for (HABFM_Circle mode : {CIRCLE_NONE, ONE_CIRCLE, TWO_CIRCLE}) {
        Outcome base = RunPair(cases[0][0], cases[0][1], mode);
        bool same = true;
        for (const auto& c : cases) {
            Outcome o = RunPair(c[0], c[1], mode);
            if (o.one != base.one || o.two != base.two) same = false;
        }
        Check(same, std::string("CircleMode=") + CircleName(mode) +
                    " 에서 5개 속도 조합의 1C/2C 상태가 모두 동일");
    }

    std::cout << "\n============================================================\n";
    if (g_failures == 0) {
        std::cout << "전부 통과 (" << g_checks << "건)\n";
    } else {
        std::cout << g_failures << " / " << g_checks << " 실패\n";
    }
    return g_failures == 0 ? 0 : 1;
}
