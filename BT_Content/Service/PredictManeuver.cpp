#include "PredictManeuver.h"
#include "PredictManeuverCsvLogger.h"
#include <cmath>

namespace Action
{
	PredictManeuver::PredictManeuver(
		const std::string& name,
		const NodeConfiguration& config
	)
		: SyncActionNode(name, config)
	{
	}

	PredictManeuver::~PredictManeuver()
	{
	}

	PortsList PredictManeuver::providedPorts()
	{
		return {
			InputPort<CPPBlackBoard*>("BB")
		};
	}

	NodeStatus PredictManeuver::tick()
	{
		// Behavior Tree로부터 블랙보드 포인터를 가져온다.
		Optional<CPPBlackBoard*> BB =
			getInput<CPPBlackBoard*>("BB");

		// 입력 포트가 없거나 포인터가 nullptr이면 실행할 수 없다.
		if (!BB || !(*BB))
		{
			return NodeStatus::FAILURE;
		}

		/*
		BFM 모드는 Rule.xml에서 이 노드보다 뒤에 결정되므로,
		직전 프레임에 stage해 둔 각도 값을 지금 확정된 BFM 모드와 함께 기록한다.
		PM_CSV_LOG가 설정되지 않았다면 아무 동작도 하지 않는다.
		*/
		PredictManeuverCsvLogger& csvLogger =
			PredictManeuverCsvLogger::Instance();

		csvLogger.FlushPending((*BB)->BFM);

		const Vector3& currentPos =
			(*BB)->TargetLocaion_Cartesian;

		const EulerAngle& currentRot =
			(*BB)->TargetRotation_EDegree;

		const float currentYaw = currentRot.Yaw;

		// 위치 기록이 historySize를 넘지 않도록
		// 가장 오래된 데이터를 제거한다.
		if (prevPositions.size() >= historySize)
		{
			prevPositions.pop_front();
		}

		// 방향 기록이 historySize를 넘지 않도록
		// 가장 오래된 데이터를 제거한다.
		if (prevHeadings.size() >= historySize)
		{
			prevHeadings.pop_front();
		}

		// 현재 프레임의 위치와 방향을 기록한다.
		prevPositions.push_back(currentPos);
		prevHeadings.push_back(currentYaw);

		// 충분한 방향 데이터가 쌓이지 않았다면
		// 아직 회전 방향을 판단하지 않는다.
		if (prevHeadings.size() < historySize)
		{
			return NodeStatus::SUCCESS;
		}

		float sumDelta = 0.0f;

		// CSV 기록용으로 가장 최근 한 쌍의 각도 값을 보관한다.
		float lastPreviousYaw = 0.0f;
		float lastCurrentYaw = 0.0f;
		float lastRawDelta = 0.0f;
		float lastNormalizedDelta = 0.0f;

		// 연속된 Yaw 값 사이의 각도 변화를 계산한다.
		for (size_t i = 1; i < prevHeadings.size(); ++i)
		{
			const float previousYaw =
				prevHeadings[i - 1];

			const float currentRecordedYaw =
				prevHeadings[i];

			const float rawDelta =
				currentRecordedYaw - previousYaw;

			// ±180도 경계를 통과할 때 발생하는
			// -358도, +358도 등의 잘못된 차이를 보정한다.
			const float normalizedDelta =
				normalizeAngleDelta(rawDelta);

			sumDelta += normalizedDelta;

			lastPreviousYaw = previousYaw;
			lastCurrentYaw = currentRecordedYaw;
			lastRawDelta = rawDelta;
			lastNormalizedDelta = normalizedDelta;
		}

		// 각도 차이의 개수는 방향 기록 개수보다 1개 적다.
		const float deltaCount =
			static_cast<float>(prevHeadings.size() - 1);

		const float avgDelta =
			sumDelta / deltaCount;

		// 이번 프레임의 각도 값을 stage 한다.
		// 실제 기록은 다음 tick에서 BFM 모드가 확정된 뒤 이루어진다.
		if (csvLogger.IsEnabled())
		{
			csvLogger.StageFrame(
				(*BB)->RunningTime,
				lastPreviousYaw,
				lastCurrentYaw,
				lastRawDelta,
				lastNormalizedDelta,
				avgDelta);
		}

		// 기존 코드의 회전 방향 기준을 그대로 유지한다.
		if (avgDelta > TURN_THRESHOLD_DEG)
		{
			(*BB)->PredictedTurnDirection = "LEFT";
		}
		else if (avgDelta < -TURN_THRESHOLD_DEG)
		{
			(*BB)->PredictedTurnDirection = "RIGHT";
		}
		else
		{
			(*BB)->PredictedTurnDirection = "STRAIGHT";
		}

		return NodeStatus::SUCCESS;
	}
}