#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../../behaviortree_cpp_v3/bt_factory.h"
#include "../BlackBoard/CPPBlackBoard.h"

#include "../../../Geometry/Vector3.h"
#include <iostream>
#include <cmath>
#include <algorithm>

namespace Action
{
    // Primary target-tracking behavior. Task_Pure remains the final fallback
    // when FollowTarget heuristics do not produce a valid pursuit waypoint.
    class Task_FollowTarget : public BT::SyncActionNode
    {
    public:
        Task_FollowTarget(const std::string& name, const BT::NodeConfiguration& config)
            : BT::SyncActionNode(name, config) {}

        static BT::PortsList providedPorts()
        {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus tick() override;

    private:
        // Pure/Lag �з� ����
        static constexpr float EPS_DEG = 4.0f;   // ������
        static constexpr float HYS_DEG = 2.0f;   // �����׸��ý�

        // Lag �Ķ����
        static constexpr float K_LAG = 0.55f;
        static constexpr float D_MIN_REF = 300.0f;   // m
        static constexpr float D_MAX_REF = 1200.0f;  // m

        // ��ǥ ����(�Ÿ�/AA/AO) ����
        static constexpr float FT2M = 0.3048f;
        static constexpr float DMOD_FT_MIN = 300.0f;     // 300 ft
        static constexpr float DMOD_FT_MAX = 3000.0f;    // 3000 ft
        static constexpr float DMOD_MIN = DMOD_FT_MIN * FT2M;  // 91.44 m
        static constexpr float DMOD_MAX = DMOD_FT_MAX * FT2M;  // 914.4 m

        static constexpr float K_RANGE_CLOSE = 0.4f; // �ּ��� LOS ���
        static constexpr float K_RANGE_TIGHT = 0.15f; // �ʹ� ������ �پ��� �� ��ȭ(Ht����)

        static constexpr float K_AA = 8.0f;    // AA �� 0��
        static constexpr float K_AO = 4.0f;    // AO �� 2��

        static inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

        static inline bool normalize(BT_Geometry::Vector3& v)
        {
            const double n = v.length();
            if (n < 1e-6) return false;
            v = v / n;
            return true;
        }

        static inline float rad2deg(float r) { return r * 57.29577951308232f; }

        static inline float safe_acos(float x)
        {
            if (x > 1.0f) x = 1.0f;
            if (x < -1.0f) x = -1.0f;
            return std::acos(x);
        }
    };
}
