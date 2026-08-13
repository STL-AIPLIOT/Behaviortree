#include "SetBFMMode_DBFM.h"
#include <iostream>
#include <algorithm>

using namespace Action;

static inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

BT::NodeStatus SetBFMMode_DBFM::tick()
{
    auto bb_ptr = getInput<CPPBlackBoard*>("BB");
    if (!bb_ptr)
    {
        std::cerr << "[SetBFMMode_DBFM] BB 포인터 가져오기 실패\n";
        return BT::NodeStatus::FAILURE;
    }

    CPPBlackBoard* BB = bb_ptr.value();

    // === 입력 (이미 BB에 존재한다고 가정; 이름은 업로드 파일 기준) ===
    const bool sight = BB->EnemyInSight;
    const float los_deg = BB->Los_Degree_Target;      // 목표 기준 시선각(가정: 작을수록 정면)
    const float D = BB->Distance;
    const float AA = BB->MyAspectAngle_Degree;        // 존재 시 사용 (없으면 999로 본다)
    const int   energy_cmp = BB->EnergyCompareResult; // >0: 우세, 0: 동등, <0: 열세

    // === DBFM 진입 창 ===
    // - 시야 확보
    // - 거리: 너무 멀면(>1500) 진입X, 너무 가까우면(예: <200) 별도 방어 스텝 필요
    // - 정면 교전/헤드온이 아닌 경우(LOS 각도가 너무 작지 않도록 완화)
    const bool dist_ok = (D >= 200.0f && D <= 1500.0f);
    const bool los_ok = (los_deg >= 15.0f); // 0~10도는 거의 정면 → DBFM 진입 억제

    if (sight && dist_ok && los_ok)
    {
        BB->BFM = DBFM;

        // === 반격 모드 조건 ===
        // 에너지 우세 + (기하 창) : 너무 가깝지 않고(Anti-overshoot 위험), 각도 과대 아님
        bool geom_ok = (D >= 350.0f && D <= 1000.0f) && (AA < 60.0f);
        BB->IsCounterAttack = (energy_cmp > 0) && geom_ok;

        std::cout << "[SetBFMMode_DBFM] t=" << BB->RunningTime << "s | Enter DBFM"
            << " | D=" << D << ", LOS=" << los_deg
            << ", AA=" << AA << ", Energy=" << energy_cmp
            << " | Counter=" << (BB->IsCounterAttack ? "YES" : "NO") << "\n";
        return BT::NodeStatus::SUCCESS;
    }

    // 진입 실패 사유 로그
    std::cout << "[SetBFMMode_DBFM] t=" << BB->RunningTime << "s | Blocked"
        << " | sight=" << sight
        << ", D=" << D << ", LOS=" << los_deg
        << ", AA=" << AA << "\n";
    return BT::NodeStatus::FAILURE;
}
