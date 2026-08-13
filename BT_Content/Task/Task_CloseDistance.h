#pragma once
#include "../../behaviortree_cpp_v3/action_node.h"
#include "../../behaviortree_cpp_v3/bt_factory.h"
#include "../BlackBoard/CPPBlackBoard.h"
#include "../../../Geometry/Vector3.h"
#include <string>
#include <cmath>
#include <iostream>
#include "../Functions.h"
#include <algorithm>


namespace Action
{
    class Task_CloseDistance : public BT::SyncActionNode
    {
    public:
        Task_CloseDistance(const std::string& name, const BT::NodeConfiguration& config)
            : BT::SyncActionNode(name, config) {}

        // XML 파라미터는 문자열로 받아서 std::stof 사용 (float convert 특수화 이슈 회피)
        static BT::PortsList providedPorts()
        {
            return {
                BT::InputPort<CPPBlackBoard*>("BB"),
                BT::InputPort<std::string>("DesiredRange"), // m (예: "400")
                BT::InputPort<std::string>("LeadTime"),     // s (예: "2.0")
                BT::InputPort<std::string>("UpBias")        // m (예: "0") 위로 살짝 띄우고 싶을 때
            };
        }

        BT::NodeStatus tick() override;
    };
}
