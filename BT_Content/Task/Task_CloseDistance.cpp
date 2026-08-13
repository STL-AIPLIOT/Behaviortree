#include "Task_CloseDistance.h"

namespace Action
{
    static inline float _len(const BT_Geometry::Vector3& v)
    {
        return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    }
    static inline BT_Geometry::Vector3 _norm(const BT_Geometry::Vector3& v)
    {
        float L = _len(v);
        if (L <= 1e-6f) return BT_Geometry::Vector3(1, 0, 0);
        return v / L;
    }

    BT::NodeStatus Task_CloseDistance::tick()
    {
        auto bbOpt = getInput<CPPBlackBoard*>("BB");
        if (!bbOpt) return BT::NodeStatus::FAILURE;
        CPPBlackBoard* bb = *bbOpt;

        if (bb->Enemy.empty()) return BT::NodeStatus::FAILURE;

        // 파라미터 파싱 (기본값)
        float desiredRange = 900.0f;
        float leadTime = 2.0f;
        float upBias = 0.0f;

        if (auto s = getInput<std::string>("DesiredRange")) { try { desiredRange = std::stof(s.value()); } catch (...) {} }
        if (auto s = getInput<std::string>("LeadTime")) { try { leadTime = std::stof(s.value()); } catch (...) {} }
        if (auto s = getInput<std::string>("UpBias")) { try { upBias = std::stof(s.value()); } catch (...) {} }

        // 타깃 예측점
        BT_Geometry::Vector3 tgtPos = bb->TargetLocaion_Cartesian;
        BT_Geometry::Vector3 tgtFwd = _norm(bb->TargetForwardVector);
        float tgtSpd = bb->TargetSpeed_MS;

        BT_Geometry::Vector3 tgtVel = tgtFwd * tgtSpd;
        BT_Geometry::Vector3 predict = tgtPos + tgtVel * leadTime;

        // 내 위치와의 거리
        BT_Geometry::Vector3 myPos = bb->MyLocation_Cartesian;
        float d = myPos.distance(predict);

        // 안전고도 보정 (1000m 미만 금지)
        float safeFloor = 1000.0f;
        float vpZ = predict.Z + upBias;
        if (vpZ < safeFloor) vpZ = safeFloor;

        // 거리 유지/감소용 VP 설정
        // - 멀면 leadTime 그대로, 충분히 가까우면 리드 타임을 줄여 과도한 오버슈트 방지
        float lt = (d > desiredRange) ? leadTime : (leadTime * 0.5f);
        BT_Geometry::Vector3 vp = tgtPos + tgtVel * lt;
        vp.Z = vpZ;

        bb->VP_Cartesian = vp;
        // 쓰로틀은 Step에서 1.0으로 덮여쓰는 구조라 여기선 건드리지 않음
        return BT::NodeStatus::SUCCESS;
    }
}
