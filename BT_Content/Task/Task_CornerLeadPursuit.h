#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../BlackBoard/CPPBlackBoard.h"
#include <iostream>

namespace Action
{
    /*
    코너 속도 구간에서만 들어가는 lead pursuit.

    3조건이 **동시에** 충족될 때만 VP를 lead point로 잡고 SUCCESS를 반환한다.
      1) IsInCornerSpeedBand  : 내 속도가 코너 속도 밴드 안 (최대 선회율 구간)
      2) IsLeadPursuitWindow  : 시야 확보 + AngleOff가 창 안 + 거리 밴드 안
      3) IsOvershootRiskFalse : 오버슛 위험 없음 (Task_AntiOvershoot과 같은 기준)

    하나라도 어긋나면 FAILURE로 양보한다. 그래서 Rule.xml에서는 일반
    Task_LeadPursuit 앞에 두어 "코너 속도일 때만 리드를 당기고, 아니면 기존 추적"
    구조가 된다.

    측정 (Docs/12 T8):
      매 tick `[Task_CornerLeadPursuit] cond | speed=? window=? noOvershoot=?` 를 찍는다.
      - 진입률          = ENTER 라인 수 / cond 라인 수
      - 어느 조건이 막았나 = cond 라인에서 0인 항목의 빈도
      진입률이 5% 미만이면 아래 상수를 완화하거나 브랜치를 통폐합한다.
    */
    class Task_CornerLeadPursuit : public BT::SyncActionNode
    {
    public:
        Task_CornerLeadPursuit(const std::string& name, const BT::NodeConfiguration& config)
            : BT::SyncActionNode(name, config) {}

        static BT::PortsList providedPorts()
        {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus tick() override;

        // --- 조건 1: 코너 속도 밴드 [m/s] ---
        // F-16 코너 속도는 대략 330~400 KCAS. 상태 배열의 속도 단위가 m/s이므로
        // 170~205 m/s 부근이다. 여유를 두고 잡았으며, 진입률을 보고 조정할 시작값이다.
        static constexpr float CORNER_SPEED_MIN_MS = 150.0f;
        static constexpr float CORNER_SPEED_MAX_MS = 210.0f;

        // --- 조건 2: lead pursuit 창 ---
        static constexpr float LEAD_WINDOW_AO_DEG = 45.0f;
        static constexpr float LEAD_D_MIN_M = 250.0f;   // Task_LeadPursuit::D_MIN과 같은 기준
        static constexpr float LEAD_D_MAX_M = 2500.0f;

        // --- 조건 3: 오버슛 위험 (Task_AntiOvershoot.cpp와 같은 기준) ---
        static constexpr float OVERSHOOT_D_M = 600.0f;
        static constexpr float OVERSHOOT_DV_MS = 12.0f;

        // lead time 상한/하한 [sec]
        static constexpr float LEAD_TIME_MIN_SEC = 0.3f;
        static constexpr float LEAD_TIME_MAX_SEC = 2.0f;
    };
}
