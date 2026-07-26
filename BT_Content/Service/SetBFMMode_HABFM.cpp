#include "SetBFMMode_HABFM.h"
#include <cmath>
#include <iostream>
#include <algorithm>

using namespace Action;

namespace
{
    constexpr double TURN_RATE_MARGIN_DEG_SEC = 2.0;        // 1C/2C 전환 데드밴드 (deg/s)
    constexpr double TURN_RATE_SAMPLE_SEC = 0.2;            // 선회율 계산용 최소 샘플 구간 (sec)
    constexpr double MAX_TURN_RATE_SAMPLE_SEC = 1.0;        // 샘플 구간 상한. 다른 BFM 분기로 tick이 끊긴 뒤의 오래된 헤딩 비교 방지 (sec)
    constexpr double MIN_VALID_DT_SEC = 1e-6;               // 0 나눗셈 방지용 최소 dt (sec)
    constexpr double MAX_VALID_TURN_RATE_DEG_SEC = 90.0;    // 비정상 선회율(수직 기동 시 Yaw 급변 등) 배제용 상한

    // 각도 차이를 [-180, 180]으로 접음 (0~360 / -180~180 표기 모두 대응)
    double WrapDeltaDegree(double delta)
    {
        while (delta > 180.0)  delta -= 360.0;
        while (delta < -180.0) delta += 360.0;
        return delta;
    }

    // 헤딩 변화량(deg)과 경과 시간(sec)으로 선회율(deg/s)을 계산. 유효하지 않으면 false
    bool CalculateTurnRateDegPerSec(double headingDeltaDegree, double dtSec, double& outputDegPerSec)
    {
        if (!std::isfinite(headingDeltaDegree) || !std::isfinite(dtSec) || dtSec <= MIN_VALID_DT_SEC)
        {
            return false;
        }

        outputDegPerSec = std::abs(WrapDeltaDegree(headingDeltaDegree)) / dtSec;

        return std::isfinite(outputDegPerSec) && outputDegPerSec <= MAX_VALID_TURN_RATE_DEG_SEC;
    }
}

/*
내 기체 / 타겟의 Yaw(deg) 변화량을 시뮬레이션 시간으로 나누어 선회율(deg/s)을 갱신
- 블랙보드에는 선회 반경/각속도 값이 없으므로 헤딩 변화 / dt 방식만 사용
- 값이 비정상이거나 샘플 구간이 모자라면 직전 선회율을 그대로 유지
*/
void SetBFMMode_HABFM::UpdateTurnRates(CPPBlackBoard* BB)
{
    const double now = BB->RunningTime;
    const double myYaw = BB->MyRotation_EDegree.Yaw;
    const double targetYaw = BB->TargetRotation_EDegree.Yaw;

    if (!std::isfinite(now) || !std::isfinite(myYaw) || !std::isfinite(targetYaw))
    {
        return;     //입력이 비정상이면 상태를 건드리지 않음
    }

    const double dt = now - PrevSampleTime_Sec;

    //첫 tick이거나, 시뮬레이션 시간이 되감겼거나, tick이 오래 끊겼던 경우 기준점만 다시 잡음
    if (!HasPrevHeading || dt < 0.0 || dt > MAX_TURN_RATE_SAMPLE_SEC)
    {
        HasPrevHeading = true;
        PrevSampleTime_Sec = now;
        PrevMyYaw_Degree = myYaw;
        PrevTargetYaw_Degree = targetYaw;
        return;
    }

    if (dt < TURN_RATE_SAMPLE_SEC)
    {
        return;     //샘플 구간 미달. 직전 선회율 유지
    }

    double myRateDeg = 0.0;
    double targetRateDeg = 0.0;

    const bool valid =
        CalculateTurnRateDegPerSec(myYaw - PrevMyYaw_Degree, dt, myRateDeg)
        &&
        CalculateTurnRateDegPerSec(targetYaw - PrevTargetYaw_Degree, dt, targetRateDeg);

    PrevSampleTime_Sec = now;
    PrevMyYaw_Degree = myYaw;
    PrevTargetYaw_Degree = targetYaw;

    if (!valid)
    {
        HasTurnRate = false;    //유효하지 않은 값은 판단에 쓰지 않음 (이전 모드 유지)
        return;
    }

    MyTurnRate_DegSec = myRateDeg;
    TargetTurnRate_DegSec = targetRateDeg;
    HasTurnRate = true;
}

/*
선회율 비교로 1C/2C 확정
- 내 선회율 우세      : 2C
- 타겟 선회율 우세    : 1C
- 차이가 데드밴드 이내: 이전 모드 유지
- 선회율이 유효하지 않으면 이전 모드 유지 (강제 전환 금지)
*/
void SetBFMMode_HABFM::UpdateCircleMode(CPPBlackBoard* BB)
{
    if (!HasTurnRate)
    {
        return;
    }

    if (MyTurnRate_DegSec >= TargetTurnRate_DegSec + TURN_RATE_MARGIN_DEG_SEC)
    {
        BB->HABFM_CircleMode = TWO_CIRCLE;
    }
    else if (TargetTurnRate_DegSec >= MyTurnRate_DegSec + TURN_RATE_MARGIN_DEG_SEC)
    {
        BB->HABFM_CircleMode = ONE_CIRCLE;
    }
    //데드밴드 이내면 BB->HABFM_CircleMode를 그대로 둠
}

BT::NodeStatus SetBFMMode_HABFM::tick()
{
    auto bb_res = getInput<CPPBlackBoard*>("BB");
    if (!bb_res)
    {
        std::cerr << "[SetBFMMode_HABFM] BB nullptr\n";
        return BT::NodeStatus::FAILURE;
    }
    CPPBlackBoard* BB = bb_res.value();

    //선회율 샘플링은 HABFM 진입 여부와 무관하게 매 tick 갱신
    UpdateTurnRates(BB);

    const bool sight = BB->EnemyInSight;
    const double aa = BB->MyAspectAngle_Degree;   // 0: 같은 방향, 180: 정반대(헤드온)
    const double D = BB->Distance;
    const int    ec = BB->EnergyCompareResult;    // >=0 권장

    // [개선] HABFM 진입 창: |AA-180| <= 40°, 800m <= D <= 2000m, 에너지 ≥ 0, 시야 必
    const bool aa_ok = (std::abs(aa - 180.0) <= 40.0);
    const bool dist_ok = (D >= 800.0 && D <= 2000.0);
    const bool e_ok = (ec >= 0);

    if (sight && aa_ok && dist_ok && e_ok)
    {
        BB->BFM = HABFM;
        UpdateCircleMode(BB);
        std::cout << "[SetBFMMode_HABFM] Enter HABFM | AA=" << aa
            << ", D=" << D << ", E=" << ec
            << " | myTR=" << MyTurnRate_DegSec << ", tgtTR=" << TargetTurnRate_DegSec
            << ", Circle=" << (BB->HABFM_CircleMode == ONE_CIRCLE ? "1C" :
                              (BB->HABFM_CircleMode == TWO_CIRCLE ? "2C" : "NONE")) << "\n";
        return BT::NodeStatus::SUCCESS;
    }

    std::cout << "[SetBFMMode_HABFM] Blocked | sight=" << sight
        << ", AA=" << aa << ", D=" << D << ", E=" << ec << "\n";
    return BT::NodeStatus::FAILURE;
}
