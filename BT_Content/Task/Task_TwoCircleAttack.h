#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../BlackBoard/CPPBlackBoard.h"
#include <iostream>

namespace Action
{
    /*
    Task_OneCircleAttack 과 같은 이유로 StatefulActionNode 다.
    Rule.xml 의 <Timeout msec="5000"> 은 자식이 RUNNING 일 때만 개입할 수 있다.

    진입 조건은 SetBFMMode_HABFM 이 선회율로 결정한 HABFM_CircleMode == TWO_CIRCLE.

    반환 의미
      RUNNING : 2-circle 기동 유지 중
      SUCCESS : 머지 해소 — AA 가 EXIT_AA_DEG 이하로 안정
      FAILURE : 지금은 2-circle 차례가 아님 -> 다음 Fallback 자식에게 양보
    */
    class Task_TwoCircleAttack : public BT::StatefulActionNode
    {
    public:
        Task_TwoCircleAttack(const std::string& name, const BT::NodeConfiguration& config)
            : BT::StatefulActionNode(name, config) {}

        static BT::PortsList providedPorts()
        {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus onStart() override;
        BT::NodeStatus onRunning() override;
        void onHalted() override;

        static constexpr float  EXIT_AA_DEG = 90.0f;
        static constexpr double SETTLE_DWELL_SEC = 0.20;

    private:
        BT::NodeStatus Advance(CPPBlackBoard* BB);
        void ResetInternalState();

        double  entry_time_sec_ = 0.0;
        double  settle_since_sec_ = -1.0;
        Vector3 vp_on_entry_;
        bool    vp_saved_ = false;
    };
}
