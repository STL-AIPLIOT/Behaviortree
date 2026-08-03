#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../../behaviortree_cpp_v3/bt_factory.h"
#include "../../../Geometry/Vector3.h"
#include "../../../Geometry/EulerAngle.h"
#include "../../../Geometry/Quaternion.h"
#include "../Functions.h"
#include "../AngleUtil.h"
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

		계산은 공통 유틸리티 BTAngle::WrapDeltaDeg 하나만 쓴다.
		여기 사본을 두면 두 구현이 갈라지므로 얇은 위임만 남긴다.
		단위·반환 범위·±180 경계 정책은 BT_Content/AngleUtil.h 참조.
		*/
		static float normalizeAngleDelta(float delta)
		{
			return BTAngle::WrapDeltaDeg(delta);
		}

		std::deque<Vector3> prevPositions;
		std::deque<float> prevHeadings;
		const size_t historySize = 5;
	};
}
