#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../../behaviortree_cpp_v3/bt_factory.h"
#include "../../../Geometry/Vector3.h"
#include "../../../Geometry/EulerAngle.h"
#include "../../../Geometry/Quaternion.h"
#include "../Functions.h"
#include "../BlackBoard/CPPBlackBoard.h"

#include <cmath>
#include <deque>
#include <string>

using namespace BT;

namespace Action
{
	class PredictManeuver : public SyncActionNode
	{
	public:
		PredictManeuver(const std::string& name, const NodeConfiguration& config);
		~PredictManeuver();

		static PortsList providedPorts();
		NodeStatus tick() override;

	private:
		// 회전 방향 판정 임계값(deg). 기존 코드의 1.5f 기준을 그대로 유지한다.
		static constexpr float TURN_THRESHOLD_DEG = 1.5f;

		/*
		연속된 Yaw 차이를 [-180, 180) 범위로 정규화한다.
		Yaw가 +179도에서 -179도로 넘어가면 raw 차이는 -358도가 되어
		실제 회전량(+2도)과 부호·크기가 모두 달라진다.
		이 wrap-around를 보정해 avgDelta 급등을 막는다.
		*/
		static float normalizeAngleDelta(float delta)
		{
			// 비정상 입력(NaN, inf)은 보정하지 않고 그대로 돌려준다.
			if (!std::isfinite(delta))
			{
				return delta;
			}

			// 360도 주기로 접어 [-180, 180) 범위에 넣는다.
			delta = std::fmod(delta + 180.0f, 360.0f);

			if (delta < 0.0f)
			{
				delta += 360.0f;
			}

			return delta - 180.0f;
		}

		std::deque<Vector3> prevPositions;
		std::deque<float> prevHeadings;
		const size_t historySize = 5;
	};
}
