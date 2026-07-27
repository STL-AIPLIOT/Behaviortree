// Task_AntiOvershoot 계약(contract) 특성 테스트
//
// 빌드/실행: tests/build_and_run_antiovershoot.sh 참고
//
// 외부 테스트 프레임워크 없이, 실패 개수를 세는 방식으로 확인한다.
// (tests/CanRetryTest.cpp 와 동일한 방식)
//
// 이 테스트가 고정하는 사실
// -------------------------
// Rule.xml:61-65 의 OBFM_Action Fallback 은
//     Task_AntiOvershoot -> Task_LeadPursuit -> Task_FollowTarget
// 순서지만, Task_AntiOvershoot.cpp:31 이 BB 가 nullptr 인 경우를 빼면
// 항상 SUCCESS 를 반환하므로 뒤의 두 가지는 도달 불가능하다.
// 또한 Task_AntiOvershoot 는 BB->Throttle 과 BB->MySpeed_MS 를 쓰지 않으므로
// 이 노드에는 속도를 특정 목표속도로 수렴시키는 제어 루프가 없다.
//
// 성격
// ----
// 이것은 "현재 동작이 이렇다"를 못박는 특성 테스트다.
// 속도 제어기를 추가하거나 Fallback 도달성을 고치면 이 테스트는 실패해야
// 하며, 그때 아래 기대값을 의도한 새 계약으로 갱신하는 것이 정상이다.

#include "../BT_Content/Task/Task_AntiOvershoot.h"
#include "../BT_Content/Task/Task_LeadPursuit.h"
#include "../behaviortree_cpp_v3/blackboard.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

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

BT::NodeConfiguration MakeConfig(BT::Blackboard::Ptr blackboard)
{
    BT::NodeConfiguration config;
    config.blackboard = blackboard;
    config.input_ports["BB"] = "{BB}";
    return config;
}

// 교전 기하를 블랙보드에 채운다.
void SetupEngagement(CPPBlackBoard& bb, float distance, float mySpeed, float targetSpeed)
{
    bb.Distance = distance;
    bb.MySpeed_MS = mySpeed;
    bb.TargetSpeed_MS = targetSpeed;
    bb.TargetLocaion_Cartesian = Vector3(distance, 0.0f, 1000.0f);
    bb.TargetForwardVector = Vector3(1.0f, 0.0f, 0.0f);
    bb.PredictedTargetVelocity = Vector3(targetSpeed, 0.0f, 0.0f);
    bb.MyLocation_Cartesian = Vector3(0.0f, 0.0f, 1000.0f);
    bb.MyForwardVector = Vector3(1.0f, 0.0f, 0.0f);
    bb.BFM = OBFM;
}

} // namespace

int main()
{
    // 노드들이 매 tick std::cout 으로 진단 로그를 뱉는다. 테스트 출력을 가리므로 버린다.
    std::streambuf* saved = std::cout.rdbuf();
    std::cout.rdbuf(nullptr);

    CPPBlackBoard bb;
    BT::Blackboard::Ptr blackboard = BT::Blackboard::create();
    CPPBlackBoard* bbPtr = &bb;
    blackboard->set("BB", bbPtr);

    const BT::NodeConfiguration config = MakeConfig(blackboard);

    Action::Task_AntiOvershoot anti("Task_AntiOvershoot", config);
    Action::Task_LeadPursuit   lead("Task_LeadPursuit", config);

    const float kNaN = std::numeric_limits<float>::quiet_NaN();

    std::cout.rdbuf(saved);
    std::cout << "== Task_AntiOvershoot 계약 테스트 ==\n";

    // 1. 트리거 조건 안(D<600, dv>12)에서 SUCCESS
    {
        std::cout << "\n[1] 오버슛 회피가 발동하는 구간\n";
        SetupEngagement(bb, 500.0f, 320.0f, 200.0f);   // D=500, dv=120

        std::cout.rdbuf(nullptr);
        const BT::NodeStatus s = anti.executeTick();
        std::cout.rdbuf(saved);

        Check(s == BT::NodeStatus::SUCCESS,
              std::string("D=500, dv=120 -> SUCCESS (got ") + ToStr(s) + ")");
    }

    // 2. 트리거 조건 밖에서도 SUCCESS (Task_AntiOvershoot.cpp:28-31 else 분기)
    {
        std::cout << "\n[2] 오버슛 회피가 발동하지 않는 구간\n";
        SetupEngagement(bb, 1500.0f, 205.0f, 200.0f);  // D=1500, dv=5

        std::cout.rdbuf(nullptr);
        const BT::NodeStatus s = anti.executeTick();
        std::cout.rdbuf(saved);

        Check(s == BT::NodeStatus::SUCCESS,
              std::string("D=1500, dv=5 -> SUCCESS (got ") + ToStr(s) + ")");
    }

    // 3. Fallback 도달성
    //    Task_LeadPursuit 는 D<250 에서 스스로 FAILURE 를 낸다(Task_LeadPursuit.cpp:20).
    //    즉 Rule.xml 의 3단 Fallback 은 앞 노드가 실패할 수 있어야 의미가 있는데,
    //    Task_AntiOvershoot 는 같은 상황에서도 SUCCESS 라 뒤가 실행되지 않는다.
    {
        std::cout << "\n[3] Rule.xml:61-65 OBFM_Action Fallback 도달성\n";
        SetupEngagement(bb, 200.0f, 320.0f, 200.0f);   // D=200 (<250)

        std::cout.rdbuf(nullptr);
        const BT::NodeStatus sLead = lead.executeTick();
        const BT::NodeStatus sAnti = anti.executeTick();
        std::cout.rdbuf(saved);

        Check(sLead == BT::NodeStatus::FAILURE,
              std::string("D=200 에서 Task_LeadPursuit 는 FAILURE (got ") + ToStr(sLead) + ")");
        Check(sAnti == BT::NodeStatus::SUCCESS,
              std::string("같은 조건에서 Task_AntiOvershoot 는 SUCCESS (got ") + ToStr(sAnti) + ")");
        Check(sAnti == BT::NodeStatus::SUCCESS,
              "따라서 Fallback 의 Task_LeadPursuit / Task_FollowTarget 은 도달 불가");
    }

    // 4. Throttle 미기록 및 속도 불변
    {
        std::cout << "\n[4] Throttle / 속도에 대한 부작용\n";
        SetupEngagement(bb, 500.0f, 320.0f, 200.0f);
        bb.Throttle = kNaN;                 // sentinel
        const float speedBefore = bb.MySpeed_MS;

        std::cout.rdbuf(nullptr);
        anti.executeTick();
        std::cout.rdbuf(saved);

        Check(std::isnan(bb.Throttle),
              "Task_AntiOvershoot 는 BB->Throttle 을 쓰지 않는다 (sentinel 유지)");
        Check(bb.MySpeed_MS == speedBefore,
              "Task_AntiOvershoot 는 BB->MySpeed_MS 를 바꾸지 않는다");
    }

    // 5. 2500ms 창 전체에서 속도 제어 동작 없음
    //    dt 는 CPPBlackBoard 생성자 기본값(CPPBlackBoard.cpp:7).
    {
        std::cout << "\n[5] 2500ms 창(449 tick) 전체 관찰\n";
        const double dt = 0.005566170;
        const int frames = static_cast<int>(2.5 / dt);   // 449

        SetupEngagement(bb, 500.0f, 320.0f, 200.0f);

        int throttleWrites = 0;
        int nonSuccess = 0;
        int speedChanges = 0;

        std::cout.rdbuf(nullptr);
        for (int f = 0; f < frames; ++f)
        {
            bb.Throttle = kNaN;
            const float speedBefore = bb.MySpeed_MS;

            if (anti.executeTick() != BT::NodeStatus::SUCCESS) ++nonSuccess;
            if (!std::isnan(bb.Throttle))                      ++throttleWrites;
            if (bb.MySpeed_MS != speedBefore)                  ++speedChanges;
        }
        std::cout.rdbuf(saved);

        Check(frames == 449,
              std::string("2500ms / dt=0.005566170 = 449 tick (got ") + std::to_string(frames) + ")");
        Check(nonSuccess == 0,
              std::string("449 tick 모두 SUCCESS (SUCCESS 아닌 tick=") + std::to_string(nonSuccess) + ")");
        Check(throttleWrites == 0,
              std::string("449 tick 중 Throttle 기록 0회 (got ") + std::to_string(throttleWrites) + ")");
        Check(speedChanges == 0,
              std::string("449 tick 중 속도 변경 0회 (got ") + std::to_string(speedChanges) + ")");
    }

    std::cout << "\n실패 " << g_failures << "건\n";
    return g_failures == 0 ? 0 : 1;
}
