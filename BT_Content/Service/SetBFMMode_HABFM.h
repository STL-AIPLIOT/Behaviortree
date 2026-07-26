#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../BlackBoard/CPPBlackBoard.h"

namespace Action
{
    class SetBFMMode_HABFM : public BT::SyncActionNode
    {
    public:
        SetBFMMode_HABFM(const std::string& name, const BT::NodeConfiguration& config)
            : BT::SyncActionNode(name, config) {}

        static BT::PortsList providedPorts()
        {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus tick() override;

    private:
        // Heading sampling state used to derive turn rates (deg/s) for the 1C/2C decision.
        void UpdateTurnRates(CPPBlackBoard* BB);
        void UpdateCircleMode(CPPBlackBoard* BB);

        bool   HasPrevHeading = false;
        bool   HasTurnRate = false;
        double PrevSampleTime_Sec = 0.0;
        double PrevMyYaw_Degree = 0.0;
        double PrevTargetYaw_Degree = 0.0;
        double MyTurnRate_DegSec = 0.0;
        double TargetTurnRate_DegSec = 0.0;
    };
}
