// 각도 wrap-around 공통 함수 + PredictManeuver 회전 방향 판정 계약 테스트
//
// 빌드/실행: tests\build_and_run_predict_angle.ps1
//
// 외부 테스트 프레임워크 없이, 실패 개수를 세는 방식으로 확인한다.
// (tests/CanRetryTest.cpp, tests/HabfmCircleModeContractTest.cpp 와 같은 형태)
//
// 검증 대상
//   1. BTAngle::SignedDeltaDeg 가 항상 최소 부호 회전량을 돌려주는가
//   2. 반환 범위가 [-180, 180) 인가 (경계 정책 포함)
//   3. 360도와 0도가 동치인가
//   4. NaN 이 보정되지 않고 그대로 통과하는가
//   5. PredictManeuver 가 ±180 경계를 지나며 회전 방향을 뒤집지 않는가
//      (= 공통 함수가 실제 판정 경로에 쓰이는가)

#include "../behaviortree_cpp_v3/bt_factory.h"
#include "../BT_Content/AngleUtil.h"
#include "../BT_Content/Service/PredictManeuver.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const std::string& what)
{
    g_checks++;
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

bool NearlyEqual(float a, float b, float tol = 1e-3f)
{
    return std::fabs(a - b) <= tol;
}

// --------------------------------------------------------------------------
// 1) 최소 부호 각도 차이 표 (과제 명세의 케이스를 그대로 옮겼다)
// --------------------------------------------------------------------------
void TestSignedDelta()
{
    std::cout << "\n[1] 최소 부호 각도 차이\n";

    struct Case
    {
        float current;
        float previous;
        float expected;
        const char* label;
    };

    // ±180 경계 정책: 범위가 반열린 [-180, 180) 이므로 항상 -180 으로 접는다.
    const std::vector<Case> cases = {
        {  10.0f,    5.0f,    5.0f, "10 - 5 = +5" },
        {   5.0f,   10.0f,   -5.0f, "5 - 10 = -5" },
        {-179.0f,  179.0f,    2.0f, "-179 - 179 = +2 (경계 통과)" },
        { 179.0f, -179.0f,   -2.0f, "179 - (-179) = -2 (경계 통과)" },
        {   1.0f,  359.0f,    2.0f, "1 - 359 = +2" },
        { 359.0f,    1.0f,   -2.0f, "359 - 1 = -2" },
        { 180.0f,    0.0f, -180.0f, "180 - 0 = -180 (경계 정책)" },
        {   0.0f,  180.0f, -180.0f, "0 - 180 = -180 (경계 정책)" },
        { 360.0f,    0.0f,    0.0f, "360 - 0 = 0" },
        {   0.0f,  360.0f,    0.0f, "0 - 360 = 0" },
    };

    for (const Case& c : cases)
    {
        const float got = BTAngle::SignedDeltaDeg(c.current, c.previous);
        Check(NearlyEqual(got, c.expected),
              std::string(c.label) + " -> " + std::to_string(got));
    }
}

// --------------------------------------------------------------------------
// 2) 반환 범위와 급등 부재
// --------------------------------------------------------------------------
void TestRange()
{
    std::cout << "\n[2] 반환 범위 [-180, 180) 과 급등 부재\n";

    bool inRange = true;
    bool noSpike = true;

    // -1080 ~ +1080 을 0.5도 간격으로 훑는다. 여러 바퀴 돈 입력도 포함된다.
    for (int i = -2160; i <= 2160; ++i)
    {
        const float delta = static_cast<float>(i) * 0.5f;
        const float got = BTAngle::WrapDeltaDeg(delta);

        if (!(got >= -180.0f && got < 180.0f))
        {
            inRange = false;
        }
        if (std::fabs(got) >= 300.0f)
        {
            noSpike = false;
        }
    }

    Check(inRange, "-1080~+1080 전 구간에서 반환값이 [-180, 180)");
    Check(noSpike, "wrap 경계에서 |값| >= 300 이 한 번도 나오지 않는다");

    // 경계 바로 아래는 179.x 로 유지되어야 한다(180 으로 튀지 않는다).
    Check(NearlyEqual(BTAngle::WrapDeltaDeg(179.9f), 179.9f), "179.9 는 그대로 179.9");
    Check(NearlyEqual(BTAngle::WrapDeltaDeg(-179.9f), -179.9f), "-179.9 는 그대로 -179.9");
    Check(NearlyEqual(BTAngle::WrapDeltaDeg(180.1f), -179.9f), "180.1 은 -179.9 로 접힌다");
}

// --------------------------------------------------------------------------
// 3) NaN / inf 처리
// --------------------------------------------------------------------------
void TestNonFinite()
{
    std::cout << "\n[3] NaN / inf 처리\n";

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    Check(std::isnan(BTAngle::WrapDeltaDeg(nan)), "NaN 은 NaN 그대로 (0 으로 위장하지 않는다)");
    Check(std::isinf(BTAngle::WrapDeltaDeg(inf)), "inf 는 inf 그대로");
    Check(std::isnan(BTAngle::SignedDeltaDeg(nan, 10.0f)), "한쪽이 NaN 이면 결과도 NaN");
}

// --------------------------------------------------------------------------
// 4) PredictManeuver 통합: ±180 경계를 지나는 좌선회
// --------------------------------------------------------------------------

// 노드를 직접 만들어 tick 하기 위한 최소 설정.
BT::NodeConfiguration MakeConfig(BT::Blackboard::Ptr bb)
{
    BT::NodeConfiguration config;
    config.blackboard = bb;
    config.input_ports["BB"] = "{BB}";
    return config;
}

// yaw 시퀀스를 한 프레임씩 흘려 넣고 마지막 판정을 돌려준다.
std::string RunYawSequence(const std::vector<float>& yaws)
{
    BT::Blackboard::Ptr bb = BT::Blackboard::create();
    CPPBlackBoard board;
    board.BFM = NONE;
    bb->set("BB", &board);

    Action::PredictManeuver node("PredictManeuver", MakeConfig(bb));

    for (size_t i = 0; i < yaws.size(); ++i)
    {
        board.RunningTime = static_cast<double>(i) * 0.1;
        board.TargetRotation_EDegree.Yaw = yaws[i];
        node.executeTick();
    }

    return board.PredictedTurnDirection;
}

void TestPredictManeuverIntegration()
{
    std::cout << "\n[4] PredictManeuver 회전 방향 판정 (경계 통과)\n";

    // historySize = 5 이므로 최소 5 프레임이 필요하다.
    // +2도씩 증가하는 좌선회가 +180 경계를 넘어간다.
    // 보정이 없으면 한 쌍의 raw 차이가 -358 이 되어 평균이 -88 로 뒤집힌다.
    const std::string crossing = RunYawSequence({176.0f, 178.0f, -180.0f, -178.0f, -176.0f});
    Check(crossing == "LEFT",
          "+180 경계를 넘는 +2도/프레임 좌선회 -> LEFT (얻은 값: " + crossing + ")");

    // 반대 방향. -180 경계를 넘는 우선회.
    const std::string crossingBack = RunYawSequence({-176.0f, -178.0f, 180.0f, 178.0f, 176.0f});
    Check(crossingBack == "RIGHT",
          "-180 경계를 넘는 -2도/프레임 우선회 -> RIGHT (얻은 값: " + crossingBack + ")");

    // 경계와 무관한 구간의 기존 동작이 그대로인지 확인한다.
    const std::string plainLeft = RunYawSequence({0.0f, 3.0f, 6.0f, 9.0f, 12.0f});
    Check(plainLeft == "LEFT", "경계와 무관한 +3도/프레임 -> LEFT (얻은 값: " + plainLeft + ")");

    const std::string straight = RunYawSequence({10.0f, 10.0f, 10.0f, 10.0f, 10.0f});
    Check(straight == "STRAIGHT", "변화 없음 -> STRAIGHT (얻은 값: " + straight + ")");

    // 임계값(1.5도) 아래의 미세한 변화는 STRAIGHT 로 남아야 한다.
    const std::string tiny = RunYawSequence({179.0f, 179.5f, -180.0f, -179.5f, -179.0f});
    Check(tiny == "STRAIGHT",
          "경계를 넘지만 +0.5도/프레임 -> STRAIGHT (얻은 값: " + tiny + ")");

    // 히스토리가 차기 전에는 판정하지 않는다(초기 프레임 처리).
    const std::string tooShort = RunYawSequence({0.0f, 5.0f, 10.0f});
    Check(tooShort.empty() || tooShort == "",
          "5프레임 미만이면 판정하지 않는다 (얻은 값: '" + tooShort + "')");
}

}  // namespace

int main()
{
    std::cout << "PredictManeuver 각도 wrap-around 계약 테스트\n";

    TestSignedDelta();
    TestRange();
    TestNonFinite();
    TestPredictManeuverIntegration();

    std::cout << "\n" << std::string(60, '=') << "\n";
    if (g_failures)
    {
        std::cout << g_failures << " / " << g_checks << " 실패\n";
    }
    else
    {
        std::cout << "전부 통과 (" << g_checks << "건)\n";
    }
    return g_failures ? 1 : 0;
}
