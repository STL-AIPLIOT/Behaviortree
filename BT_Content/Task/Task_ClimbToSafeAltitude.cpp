#include "Task_ClimbToSafeAltitude.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Action {

    Task_ClimbToSafeAltitude::Task_ClimbToSafeAltitude(const std::string& name, const NodeConfiguration& config)
        : SyncActionNode(name, config) {
    }

    Task_ClimbToSafeAltitude::~Task_ClimbToSafeAltitude() {}

    PortsList Task_ClimbToSafeAltitude::providedPorts() {
        return { InputPort<CPPBlackBoard*>("BB") };
    }

    static inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
    static inline float length3(const Vector3& v) { return std::sqrt(float(v.X * v.X + v.Y * v.Y + v.Z * v.Z)); }

    NodeStatus Task_ClimbToSafeAltitude::tick() {
        auto BBopt = getInput<CPPBlackBoard*>("BB");
        if (!BBopt || !BBopt.value()) return NodeStatus::FAILURE;
        CPPBlackBoard* BB = BBopt.value();

        const Vector3 myPos = BB->MyLocation_Cartesian;
        const Vector3 fwd = BB->MyForwardVector;
        const Vector3 up = BB->MyUpVector;
        const Vector3 right = BB->MyRightVector;

        // 근사 강하율: 전방벡터 Z성분 × 속도
        const float Vz_est = float(fwd.Z) * BB->MySpeed_MS;
        const float pitch_deg = std::asin(clampf(float(fwd.Z), -1.0f, 1.0f)) * 57.29578f;

        // 안전고도(ft 또는 m, 프로젝트 단위에 맞추세요)
        const float kFloor = 1200.0f;
        const float margin = 200.0f;
        const float curZ = float(myPos.Z);

        // ---- 핵심 1: "수평 전방"을 만들어 Z 음수 기여 제거 ----
        // fwd_h = fwd - up * dot(fwd,up)  (수평면으로 투영 후 정규화)
        float dot_fu = float(fwd.X * up.X + fwd.Y * up.Y + fwd.Z * up.Z);
        Vector3 fwd_h = fwd - up * dot_fu;
        float mag = length3(fwd_h);
        if (mag < 1e-3f) {
            // 전방이 거의 위/아래로 향하면 수평 전방을 Right로 대체
            fwd_h = right;
            mag = length3(fwd_h);
            if (mag < 1e-3f) {
                // 혹시 모를 특이 케이스
                fwd_h = Vector3{ 1,0,0 };
                mag = 1.0f;
            }
        }
        fwd_h = fwd_h * (1.0f / mag); // 정규화

        // ---- 오프셋 크기: 속도 기반 가변 ----
        const float spd = std::max(50.0f, BB->MySpeed_MS); // 안전 하한
        const float ahead = clampf(150.0f + 1.2f * spd, 200.0f, 600.0f);
        const float climb = clampf(200.0f + 0.6f * spd, 250.0f, 800.0f);

        // ---- 목표점: "수평 전방 + 순수 상승" → Z는 언제나 증가 ----
        Vector3 vp = myPos + fwd_h * ahead + up * climb;

        // ---- BFM 차단(선택적이지만 강력 추천): 회복 중엔 다른 BFM 루트 진입 방지 ----
        // DECO_BFMCheck들이 BB->BFM을 검사하므로 NONE으로 비워두면 대부분 전략이 차단됨
        BB->BFM = NONE;

        // ---- 스로틀/VP 적용 ----
        BB->VP_Cartesian = vp;
        BB->Throttle = 1.0f;

        // ---- 종료 조건: 충분히 안전해졌는가? ----
        const bool safe_alt = (curZ >= (kFloor + margin));
        const bool good_pitch = (pitch_deg >= 2.0f);
        const bool climbing = (Vz_est > 5.0f);

        std::cout << "[Task_ClimbToSafeAltitude] Recovering"
            << " | pitch=" << pitch_deg
            << " Vz_est=" << Vz_est
            << " | ahead=" << ahead << " climb=" << climb
            << " | fwd_hZ=0 targetZ=" << vp.Z << " curZ=" << curZ
            << " | safe=" << (safe_alt && good_pitch && climbing ? "YES" : "NO")
            << "\n";

        // ---- 핵심 2: 회복 완료 전에는 RUNNING 유지 (다른 루트 실행 방지) ----
        if (safe_alt && good_pitch && climbing) {
            return NodeStatus::SUCCESS;   // 이제 다른 트리로 돌아가도 안전
        }
        else {
            return NodeStatus::RUNNING;   // 회복 계속: 같은 노드가 우선권을 유지
        }
    }

} // namespace Action
