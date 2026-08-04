// CanRetry 데코레이터 상태 전이 테스트
//
// 빌드/실행: tests/build_and_run.sh 참고
//
// 외부 테스트 프레임워크 없이, 실패 개수를 세는 방식으로 확인한다.

#include "../behaviortree_cpp_v3/bt_factory.h"
#include "../BT_Content/Decorator/DECO_CanRetry.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{

int g_failures = 0;

void Check(bool condition, const std::string& what)
{
    if (condition)
    {
        std::cout << "  [OK]   " << what << "\n";
    }
    else
    {
        std::cout << "  [FAIL] " << what << "\n";
        g_failures++;
    }
}

const char* ToStr(BT::NodeStatus s)
{
    switch (s)
    {
        case BT::NodeStatus::IDLE:    return "IDLE";
        case BT::NodeStatus::RUNNING: return "RUNNING";
        case BT::NodeStatus::SUCCESS: return "SUCCESS";
        case BT::NodeStatus::FAILURE: return "FAILURE";
    }
    return "?";
}

// 테스트용 액션 노드가 반환할 상태를 미리 정해두고, 실행/중단 횟수를 센다.
struct MockScript
{
    std::vector<BT::NodeStatus> statuses;
    size_t index = 0;
    int tick_count = 0;
    int halt_count = 0;

    void Reset(std::vector<BT::NodeStatus> s)
    {
        statuses = std::move(s);
        index = 0;
        tick_count = 0;
        halt_count = 0;
    }

    BT::NodeStatus Next()
    {
        if (statuses.empty())
        {
            return BT::NodeStatus::FAILURE;
        }
        const size_t i = (index < statuses.size()) ? index : statuses.size() - 1;
        index++;
        return statuses[i];
    }
};

MockScript g_mock;
int g_next_branch_ticks = 0;

class MockAction : public BT::ActionNodeBase
{
public:
    MockAction(const std::string& name, const BT::NodeConfiguration& config)
        : BT::ActionNodeBase(name, config) {}

    static BT::PortsList providedPorts() { return {}; }

    BT::NodeStatus tick() override
    {
        g_mock.tick_count++;
        return g_mock.Next();
    }

    void halt() override
    {
        g_mock.halt_count++;
        setStatus(BT::NodeStatus::IDLE);
    }
};

// Fallback의 다음 순위 분기가 실행되는지 확인하기 위한 노드
class MockNextBranch : public BT::SyncActionNode
{
public:
    MockNextBranch(const std::string& name, const BT::NodeConfiguration& config)
        : BT::SyncActionNode(name, config) {}

    static BT::PortsList providedPorts() { return {}; }

    BT::NodeStatus tick() override
    {
        g_next_branch_ticks++;
        return BT::NodeStatus::SUCCESS;
    }
};

BT::BehaviorTreeFactory MakeFactory()
{
    BT::BehaviorTreeFactory factory;
    factory.registerNodeType<Action::DECO_CanRetry>("CanRetry");
    factory.registerNodeType<MockAction>("MockAction");
    factory.registerNodeType<MockNextBranch>("MockNextBranch");
    return factory;
}

std::string CanRetryTreeXML(int max_retries, int msec)
{
    return
        "<root main_tree_to_execute=\"MainTree\">"
        "  <BehaviorTree ID=\"MainTree\">"
        "    <CanRetry max_retries=\"" + std::to_string(max_retries) + "\" BB=\"{BB}\">"
        "      <Timeout msec=\"" + std::to_string(msec) + "\">"
        "        <MockAction/>"
        "      </Timeout>"
        "    </CanRetry>"
        "  </BehaviorTree>"
        "</root>";
}

// DBFM_Action과 같은 구조: CanRetry 분기가 실패하면 다음 순위 분기로 넘어가야 한다.
std::string FallbackTreeXML(int max_retries, int msec)
{
    return
        "<root main_tree_to_execute=\"MainTree\">"
        "  <BehaviorTree ID=\"MainTree\">"
        "    <Fallback>"
        "      <CanRetry max_retries=\"" + std::to_string(max_retries) + "\" BB=\"{BB}\">"
        "        <Timeout msec=\"" + std::to_string(msec) + "\">"
        "          <MockAction/>"
        "        </Timeout>"
        "      </CanRetry>"
        "      <MockNextBranch/>"
        "    </Fallback>"
        "  </BehaviorTree>"
        "</root>";
}

void Sleep(int msec)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(msec));
}

} // namespace

int main()
{
    CPPBlackBoard BB;

    // 1. 자식 RUNNING -> CanRetry RUNNING
    {
        std::cout << "[1] child RUNNING\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::RUNNING });

        const auto s = tree.tickRoot();
        Check(s == BT::NodeStatus::RUNNING, std::string("RUNNING 반환 (got ") + ToStr(s) + ")");
        Check(g_mock.tick_count == 1, "자식 1회 실행");
        Check(g_mock.halt_count == 0, "자식 halt 없음");
    }

    // 2. 자식 SUCCESS -> 상태 초기화 후 SUCCESS
    {
        std::cout << "[2] child SUCCESS\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::SUCCESS });

        Check(tree.tickRoot() == BT::NodeStatus::SUCCESS, "SUCCESS 반환");
        Check(tree.tickRoot() == BT::NodeStatus::SUCCESS, "재진입 시에도 SUCCESS");
    }

    // 3. timeout 발생 -> 다음 tick에서 재시도 시작, 소진 후 FAILURE
    {
        std::cout << "[3] timeout -> next tick retry\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 50));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::RUNNING });

        Check(tree.tickRoot() == BT::NodeStatus::RUNNING, "1차 시도 RUNNING");
        Sleep(120);
        const auto s2 = tree.tickRoot();
        Check(s2 == BT::NodeStatus::RUNNING, std::string("timeout 후에도 RUNNING (재시도 예약) (got ") + ToStr(s2) + ")");
        Check(g_mock.halt_count >= 1, "timeout 시 자식 halt");

        const int ticks_before = g_mock.tick_count;
        const auto s3 = tree.tickRoot();
        Check(g_mock.tick_count == ticks_before + 1, "다음 tick에서 2차 시도 실행");
        Check(s3 == BT::NodeStatus::RUNNING, "2차 시도 RUNNING (타이머 재시작)");

        Sleep(120);
        const auto s4 = tree.tickRoot();
        Check(s4 == BT::NodeStatus::FAILURE, std::string("재시도 소진 후 FAILURE (got ") + ToStr(s4) + ")");
    }

    // 4. 1차 실패 후 재시도 성공
    {
        std::cout << "[4] fail -> retry success\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::FAILURE, BT::NodeStatus::SUCCESS });

        Check(tree.tickRoot() == BT::NodeStatus::RUNNING, "1차 실패 -> RUNNING");
        Check(g_mock.tick_count == 1, "같은 tick에서 재실행하지 않음");
        Check(tree.tickRoot() == BT::NodeStatus::SUCCESS, "다음 tick에서 재시도 성공");
        Check(g_mock.tick_count == 2, "총 2회 실행");
    }

    // 5. 모두 실패 -> FAILURE, Fallback 다음 분기 진행
    {
        std::cout << "[5] all attempts fail -> fallback continues\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(FallbackTreeXML(1, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::FAILURE });
        g_next_branch_ticks = 0;

        Check(tree.tickRoot() == BT::NodeStatus::RUNNING, "1차 실패 -> RUNNING");
        Check(g_next_branch_ticks == 0, "재시도 중에는 다음 분기로 넘어가지 않음");
        Check(tree.tickRoot() == BT::NodeStatus::SUCCESS, "재시도까지 실패 후 다음 분기 실행");
        Check(g_next_branch_ticks == 1, "다음 순위 분기 1회 실행");
        Check(g_mock.tick_count == 2, "반격 시도는 총 2회");
    }

    // 6. 상위 노드 halt -> 자식, timeout, 재시도 카운터 초기화
    {
        std::cout << "[6] parent halt resets state\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::FAILURE });

        Check(tree.tickRoot() == BT::NodeStatus::RUNNING, "1차 실패 -> RUNNING (retry_count=1)");
        tree.haltTree();
        Check(tree.tickRoot() == BT::NodeStatus::RUNNING,
              "halt 후 첫 시도부터 다시 시작 (카운터 초기화)");
    }

    // 6-b. 상위 노드 halt -> Timeout 타이머도 초기화
    {
        std::cout << "[6-b] parent halt resets timeout timer\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 50));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::RUNNING });

        Check(tree.tickRoot() == BT::NodeStatus::RUNNING, "1차 시도 RUNNING");
        tree.haltTree();
        Sleep(120);
        const auto s = tree.tickRoot();
        Check(s == BT::NodeStatus::RUNNING,
              std::string("halt 후 이전 타이머가 남아 즉시 실패하지 않음 (got ") + ToStr(s) + ")");
    }

    // 7. 반격 조건이 무효가 되면 재시도하지 않는다
    {
        std::cout << "[7] counterattack no longer valid\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = false;
        g_mock.Reset({ BT::NodeStatus::FAILURE });

        const auto s = tree.tickRoot();
        Check(s == BT::NodeStatus::FAILURE, std::string("재시도 없이 FAILURE (got ") + ToStr(s) + ")");
        Check(g_mock.tick_count == 1, "자식은 1회만 실행");
        BB.IsCounterAttack = true;
    }

    // 8. 재진입 시 재시도 카운터/타이머가 깨끗한 상태로 시작
    {
        std::cout << "[8] clean state on re-entry\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(1, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::FAILURE });

        Check(tree.tickRoot() == BT::NodeStatus::RUNNING, "1주기: 1차 실패 -> RUNNING");
        Check(tree.tickRoot() == BT::NodeStatus::FAILURE, "1주기: 재시도 실패 -> FAILURE");
        Check(tree.tickRoot() == BT::NodeStatus::RUNNING, "2주기: 다시 1차 시도부터 시작");
        Check(tree.tickRoot() == BT::NodeStatus::FAILURE, "2주기: 재시도 실패 -> FAILURE");
        Check(g_mock.tick_count == 4, "주기마다 최대 2회씩 실행");
    }

    // 9. max_retries=0 이면 재시도하지 않는다 (무한 재시도 방지)
    {
        std::cout << "[9] max_retries=0\n";
        auto factory = MakeFactory();
        auto tree = factory.createTreeFromText(CanRetryTreeXML(0, 4000));
        tree.rootBlackboard()->set<CPPBlackBoard*>("BB", &BB);
        BB.IsCounterAttack = true;
        g_mock.Reset({ BT::NodeStatus::FAILURE });

        Check(tree.tickRoot() == BT::NodeStatus::FAILURE, "즉시 FAILURE");
        Check(g_mock.tick_count == 1, "자식은 1회만 실행");
    }

    std::cout << "\n실패한 검증: " << g_failures << "\n";
    return (g_failures == 0) ? 0 : 1;
}
