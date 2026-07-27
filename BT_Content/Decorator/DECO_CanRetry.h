#pragma once
#include "../../behaviortree_cpp_v3/decorator_node.h"
#include "../BlackBoard/CPPBlackBoard.h"

namespace Action
{
    /**
     * CanRetry (XML 태그 이름: CanRetry)
     *
     * 자식이 FAILURE를 반환했을 때, 남은 재시도 횟수가 있으면
     * 자식을 halt() 해서 초기화하고 RUNNING을 반환하는 데코레이터이다.
     * 실제 재시도는 같은 tick 안에서 while 루프로 돌지 않고,
     * 다음 BT tick에서 이루어진다.
     *
     * 사용 예시:
     *
     * <CanRetry max_retries="1" BB="{BB}">
     *     <Timeout msec="4000">
     *         <DECO_CounterAttackCheck BB="{BB}">
     *             <Task_RollReverseAttack BB="{BB}"/>
     *         </DECO_CounterAttackCheck>
     *     </Timeout>
     * </CanRetry>
     *
     * max_retries = 1이면 최초 시도 1회 + 추가 시도 1회, 총 2회를 시도한다.
     *
     * BT::RetryNode(RetryUntilSuccesful)와 다른 점:
     * - RetryNode는 같은 tick 안에서 while 루프로 즉시 재시도하므로
     *   Timeout 타이머가 초기화되지 않고 한 tick이 길게 묶인다.
     * - CanRetry는 자식을 halt() 해서 Timeout 타이머까지 초기화한 뒤
     *   RUNNING을 반환하고, 다음 tick에서 새 시도를 시작한다.
     * - 재시도 직전에 반격 가능 조건(BB->IsCounterAttack)을 다시 확인한다.
     */
    class DECO_CanRetry : public BT::DecoratorNode
    {
    public:
        DECO_CanRetry(const std::string& name, const BT::NodeConfiguration& config)
            : BT::DecoratorNode(name, config), retry_count_(0), max_retries_(0) {}

        static BT::PortsList providedPorts()
        {
            return {
                BT::InputPort<int>(MAX_RETRIES, 1,
                    "Retry a failing child up to N additional times. "
                    "0 disables the retry."),
                BT::InputPort<CPPBlackBoard*>("BB")
            };
        }

        BT::NodeStatus tick() override;

        /// 상위 노드가 중단시키면 자식과 재시도 카운터를 모두 초기화한다.
        void halt() override;

    private:
        /// 재시도 직전에 반격 조건이 아직 유효한지 확인한다.
        bool IsCounterAttackValid();

        static constexpr const char* MAX_RETRIES = "max_retries";

        /// 현재까지 추가로 시도한 횟수
        int retry_count_;

        /// XML max_retries 포트에서 읽어온 최대 추가 시도 횟수
        int max_retries_;
    };
}
