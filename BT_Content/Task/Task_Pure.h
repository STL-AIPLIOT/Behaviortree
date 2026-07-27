#pragma once

#include "../../behaviortree_cpp_v3\action_node.h"
#include "../../behaviortree_cpp_v3/bt_factory.h"
#include "../../../Geometry/Vector3.h"
#include "../Functions.h"
#include "../BlackBoard/CPPBlackBoard.h"

using namespace BT;

namespace Action
{
	// Simple final fallback that directs the aircraft straight toward the target
	// when more advanced tracking behavior is unavailable.
	class Task_Pure : public SyncActionNode
	{
	private:


	public:


		Task_Pure(const std::string& name, const NodeConfiguration& config) : SyncActionNode(name, config)
		{
		}

		~Task_Pure()
		{

		}

		static PortsList providedPorts();

		NodeStatus tick() override;
	};
}