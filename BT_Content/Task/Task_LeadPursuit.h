#pragma once
#include "../../behaviortree_cpp_v3/action_node.h"
#include "../BlackBoard/CPPBlackBoard.h"
#include <iostream>

namespace Action {  // ★ 추가

    /*
    Rule.xml:69 에서 이 노드는 <Timeout msec="3500"> 으로 감싸여 있다.
    TimeoutNode 는 자식이 RUNNING 인 동안에만 halt 를 걸 수 있다
    (decorators/timeout_node.cpp:74). SyncActionNode 는 RUNNING 을 반환하면
    예외를 던지므로(action_node.h:60) Timeout 이 절대 발화하지 못한다.
    그래서 StatefulActionNode 로 전환한다.

    상태 의미
      RUNNING : lead pursuit 추종을 계속 수행 중
      SUCCESS : 추종 목표 달성(기수 교차각이 SETTLE_AO_DEG 이하로 수렴 유지)
      FAILURE : 기하가 무효하여 다음 Fallback 자식에게 양보 (D < D_MIN)
    */
    class Task_LeadPursuit : public BT::StatefulActionNode {
    public:
        Task_LeadPursuit(const std::string& name, const BT::NodeConfiguration& config)
            : BT::StatefulActionNode(name, config) {}

        static BT::PortsList providedPorts() {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus onStart() override;
        BT::NodeStatus onRunning() override;
        void onHalted() override;

        // 기존 구현에 있던 유일한 무효 기하 임계값 (변경 전 Task_LeadPursuit.cpp:19)
        static constexpr float D_MIN = 250.0f;

        // 추종 목표 달성 판정 임계값.
        // 기수 교차각이 이 값 이하로 SETTLE_DWELL_SEC 동안 유지되면 lead 해를 확보한 것으로 본다.
        static constexpr float  SETTLE_AO_DEG = 10.0f;
        static constexpr double SETTLE_DWELL_SEC = 0.20;

    private:
        // onStart / onRunning 이 공유하는 본체.
        BT::NodeStatus Advance(CPPBlackBoard* BB);

        // 재진입 시 이전 실행이 남긴 값이 보이지 않도록 전부 되돌린다.
        void ResetInternalState();

        double  entry_time_sec_ = 0.0;

        // 목표 조건을 만족하기 시작한 시각. 음수면 아직 만족한 적 없음.
        double  settle_since_sec_ = -1.0;

        // halt 시 되돌릴 진입 직전의 추종점. 중단된 기동의 명령이 남지 않게 한다.
        Vector3 vp_on_entry_;
        bool    vp_saved_ = false;
    };

} // namespace Action
