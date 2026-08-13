#include "Task_LeadPursuit.h"
#include "LeadPursuitTelemetry.h"
#include <algorithm>

namespace Action {  // ★ 추가

    static inline float clampf(float v, float lo, float hi) {
        return std::max(lo, std::min(hi, v));
    }

    void Task_LeadPursuit::ResetInternalState() {
        entry_time_sec_ = 0.0;
        settle_since_sec_ = -1.0;
        vp_saved_ = false;
    }

    BT::NodeStatus Task_LeadPursuit::onStart() {
        auto bb_res = getInput<CPPBlackBoard*>("BB");
        if (!bb_res) {
            std::cerr << "[Task_LeadPursuit] BB nullptr\n";
            ResetInternalState();
            return BT::NodeStatus::FAILURE;
        }
        CPPBlackBoard* BB = bb_res.value();

        // Timeout 이후 재진입할 때 직전 실행 상태가 이어지지 않도록 항상 초기화하고 시작한다.
        ResetInternalState();

        entry_time_sec_ = BB->RunningTime;
        vp_on_entry_ = BB->VP_Cartesian;
        vp_saved_ = true;

        LeadPursuitTelemetry::Instance().OnEntry(*BB);

        return Advance(BB);
    }

    BT::NodeStatus Task_LeadPursuit::onRunning() {
        auto bb_res = getInput<CPPBlackBoard*>("BB");
        if (!bb_res) {
            std::cerr << "[Task_LeadPursuit] BB nullptr\n";
            ResetInternalState();
            return BT::NodeStatus::FAILURE;
        }

        return Advance(bb_res.value());
    }

    BT::NodeStatus Task_LeadPursuit::Advance(CPPBlackBoard* BB) {
        const double elapsed = BB->RunningTime - entry_time_sec_;

        const float D = BB->Distance;
        if (D < D_MIN) { // [변경] 기존과 동일한 무효 기하 조건
            // 다음 Fallback 자식에게 양보한다. 이 노드가 쓴 추종점은 남기지 않는다.
            if (vp_saved_) {
                BB->VP_Cartesian = vp_on_entry_;
            }
            LeadPursuitTelemetry::Instance().OnExit("FAILURE", "FAILURE", *BB, elapsed);
            ResetInternalState();
            return BT::NodeStatus::FAILURE;
        }

        const float tgt_spd = std::max(1.0f, BB->TargetSpeed_MS);
        const float lead_time = std::max(0.3f, std::min(2.0f, D / tgt_spd)); // [변경]

        BB->VP_Cartesian = BB->TargetLocaion_Cartesian + BB->PredictedTargetVelocity * lead_time;

        // 추종 목표 달성 판정. 한 프레임 떨림으로 즉시 SUCCESS 가 되지 않도록 유지 시간을 본다.
        const float ao = BB->MyAngleOff_Degree;

        if (ao <= SETTLE_AO_DEG) {
            if (settle_since_sec_ < 0.0) {
                settle_since_sec_ = BB->RunningTime;
            }

            if (BB->RunningTime - settle_since_sec_ >= SETTLE_DWELL_SEC) {
                // 목표 달성. 추종점(VP_Cartesian)은 유효한 명령이므로 그대로 둔다.
                LeadPursuitTelemetry::Instance().OnExit("SUCCESS", "SUCCESS", *BB, elapsed);
                ResetInternalState();
                return BT::NodeStatus::SUCCESS;
            }
        }
        else {
            settle_since_sec_ = -1.0;
        }

        LeadPursuitTelemetry::Instance().OnTick(*BB, elapsed);
        return BT::NodeStatus::RUNNING;
    }

    void Task_LeadPursuit::onHalted() {
        // Timeout(Rule.xml:69) 만료 시 타이머 스레드에서 haltChild() 를 거쳐 들어온다.
        // decorators/timeout_node.cpp:77 -> decorator_node.cpp:53 -> 여기.
        auto bb_res = getInput<CPPBlackBoard*>("BB");

        if (bb_res && vp_saved_) {
            CPPBlackBoard* BB = bb_res.value();

            // 중단된 기동의 추종점이 남아 다음 자식의 명령과 섞이지 않게 되돌린다.
            BB->VP_Cartesian = vp_on_entry_;

            LeadPursuitTelemetry::Instance().OnExit(
                "FAILURE", "HALTED", *BB, BB->RunningTime - entry_time_sec_);
        }

        ResetInternalState();
    }

} // namespace Action
