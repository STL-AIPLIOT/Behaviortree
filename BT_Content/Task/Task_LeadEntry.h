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
    class Task_LeadEntry : public BT::SyncActionNode
    {
    public:
        Task_LeadEntry(const std::string& name, const BT::NodeConfiguration& config)
            : BT::SyncActionNode(name, config) {}

        static BT::PortsList providedPorts()
        {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus tick() override;

    private:
        float lead_timer_ = 0.0f;

        // ���� Ÿ�̹�(��)
        static constexpr float T_MIN = 0.6f;
        static constexpr float T_MAX = 2.0f;

        // ���� ��� �Ÿ�(ft �� m). �ָ����� ª�� ���� ����
        static constexpr float FT2M = 0.3048f;
        static constexpr float LEAD_FT_MIN = 3000.0f;   // 1500 ft
        static constexpr float LEAD_FT_MAX = 15000.0f;   // 3000 ft
        static constexpr float LEAD_D_MIN = LEAD_FT_MIN * FT2M;
        static constexpr float LEAD_D_MAX = LEAD_FT_MAX * FT2M;

        // ���� ���� �ð� ����(���� ����)
        static constexpr float LEAD_TIME_MAX = 2.5f;    // s

        // AA ����(�ʹ� ũ�� ����)
        static constexpr float AA_MAX = 45.0f;         // deg

        // ��ǥ ���� ���� ���� (deg �� m ������ ����, �Ÿ� ��� ���)
        static constexpr float K_AA = 4.0f;          // AA �� 0��
        static constexpr float K_AO = 2.0f;          // AO �� 2��

        static inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

        static inline bool normalize(BT_Geometry::Vector3& v)
        {
            const double n = v.length();
            if (n < 1e-6) return false;
            v = v / n;
            return true;
        }
    };
}
