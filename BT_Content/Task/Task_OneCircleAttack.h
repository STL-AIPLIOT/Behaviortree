#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../BlackBoard/CPPBlackBoard.h"
#include <iostream>

namespace Action
{
    /*
    Rule.xml 의 HABFM_Action 은 이 노드를 <Timeout msec="5000"> 으로 감싼다.
    TimeoutNode 는 자식이 RUNNING 인 동안에만 halt 할 수 있으므로
    (decorators/timeout_node.cpp:74), SyncActionNode 로 두면 Timeout 이 아무 일도
    하지 않는다. Task_LeadPursuit 과 같은 이유로 StatefulActionNode 를 쓴다.

    진입 조건은 더 이상 속도 비교가 아니라 SetBFMMode_HABFM 이 선회율로 결정해
    블랙보드에 넣어 둔 HABFM_CircleMode 다. 예전처럼 각 Task 가 자기 기준으로
    분기하면 myV == tgV 에서 1C/2C 가 동시에 실패해 HABFM 이 통째로 무행동이 된다.

    반환 의미
      RUNNING : 1-circle 기동 유지 중
      SUCCESS : 머지 해소 — 앵글을 얻어 AA 가 EXIT_AA_DEG 이하로 안정
      FAILURE : 지금은 1-circle 차례가 아님 -> 다음 Fallback 자식에게 양보
    */
    class Task_OneCircleAttack : public BT::StatefulActionNode
    {
    public:
        Task_OneCircleAttack(const std::string& name, const BT::NodeConfiguration& config)
            : BT::StatefulActionNode(name, config) {}

        static BT::PortsList providedPorts()
        {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus onStart() override;
        BT::NodeStatus onRunning() override;
        void onHalted() override;

        // 머지 해소 판정. AA 가 이 값 이하로 SETTLE_DWELL_SEC 동안 유지되면 SUCCESS.
        // HABFM 진입창이 |AA-180| <= 40 도(= AA 140 이상)이므로, 90 도까지 떨어졌다는 것은
        // 헤드온을 벗어나 앵글을 얻었다는 뜻이다.
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
