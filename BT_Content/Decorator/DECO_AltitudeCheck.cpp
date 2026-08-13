#include "DECO_AltitudeCheck.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace Action {

    DECO_AltitudeCheck::DECO_AltitudeCheck(const std::string& name, const NodeConfiguration& config)
        : SyncActionNode(name, config) {
    }

    DECO_AltitudeCheck::~DECO_AltitudeCheck() {}

    PortsList DECO_AltitudeCheck::providedPorts() {
        return {
            InputPort<CPPBlackBoard*>("BB"),
            InputPort<std::string>("UpDown"),
            InputPort<std::string>("Altitude")
        };
    }

    static inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

    NodeStatus DECO_AltitudeCheck::tick() {
        auto BBopt = getInput<CPPBlackBoard*>("BB");
        auto updownO = getInput<std::string>("UpDown");
        auto altO = getInput<std::string>("Altitude");
        if (!BBopt || !updownO || !altO) return NodeStatus::FAILURE;

        CPPBlackBoard* BB = BBopt.value();
        const std::string mode = updownO.value();
        const float targetAlt = std::stof(altO.value());

        // 현재 고도
        const float Z = static_cast<float>(BB->MyLocation_Cartesian.Z);

        // ★ 강하율 근사(벡터 없이): 전방벡터의 Z성분 × 속도(m/s)
        const float Vz_est = static_cast<float>(BB->MyForwardVector.Z) * BB->MySpeed_MS;

        // 기수 피치(도): 전방벡터 Z성분으로 근사
        const float pitch_deg = std::asin(clampf((float)BB->MyForwardVector.Z, -1.0f, 1.0f)) * 57.29578f;

        // 2초 후 예측 고도
        const float horizon = 2.0f;
        const float Z_pred = Z + Vz_est * horizon;

        if (mode == "Greater") {
            return (Z > targetAlt) ? NodeStatus::SUCCESS : NodeStatus::FAILURE;
        }
        else if (mode == "Less") {
            // (1) 현재 고도 미만
            if (Z < targetAlt) return NodeStatus::SUCCESS;

            // (2) 여유 고도라도 급하강/기수하강이면 조기 개입
            const bool bad_descent = (Vz_est < -15.0f) || (pitch_deg < -5.0f);
            if ((Z < targetAlt + 150.0f) && bad_descent) return NodeStatus::SUCCESS;

            // (3) 예측 고도 미만
            if (Z_pred < targetAlt) return NodeStatus::SUCCESS;

            return NodeStatus::FAILURE;
        }
        else {
            return NodeStatus::FAILURE;
        }
    }

} // namespace Action
