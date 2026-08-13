#pragma once
#include "../../behaviortree_cpp_v3/action_node.h"
#include "../BlackBoard/CPPBlackBoard.h"
#include <iostream>

namespace Action {  // ¡Ú Ãß°¡

    class Task_AntiOvershoot : public BT::SyncActionNode {
    public:
        Task_AntiOvershoot(const std::string& name, const BT::NodeConfiguration& config)
            : BT::SyncActionNode(name, config) {}

        static BT::PortsList providedPorts() {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus tick() override;
    };

} // namespace Action
