#pragma once

/*
PredictManeuver의 각도 wrap-around 수정 효과를 프레임 단위로 검증하기 위한
전용 CSV 로거이다. 기존 로거(bt_file_logger, minitrace 등)와는 독립적으로
동작하며 다른 로깅 경로를 변경하지 않는다.

활성화 조건:
  환경변수 PM_CSV_LOG 에 출력 파일 경로가 지정된 경우에만 기록한다.
  지정되지 않으면 시뮬레이션 동작에 영향이 없도록 모든 호출이 즉시 반환된다.

환경변수:
  PM_CSV_LOG      출력 CSV 경로 (예: logs/predict_after.csv)
  PM_CSV_RUNTYPE  runType 컬럼 값 (예: before, after). 미지정 시 "unknown"

기록 컬럼:
  runType,frame,time,prevAngle,currAngle,rawDelta,normalizedDelta,
  avgDelta,bfmMode,scissorsEntered

BFM 모드는 Rule.xml 상 PredictManeuver보다 뒤에서 결정되므로,
한 프레임의 각도 값은 stage 해 두었다가 다음 tick 시작 시점에
확정된 BFM 모드와 함께 기록한다(1 프레임 지연 flush).
*/

#include "../BlackBoard/CPPBlackBoard.h"

#include <fstream>
#include <string>

namespace Action
{
	class PredictManeuverCsvLogger
	{
	public:
		// 프로세스 전역 단일 인스턴스. 첫 호출 시 환경변수를 읽어 파일을 연다.
		static PredictManeuverCsvLogger& Instance();

		// 로깅이 켜져 있는지 여부. 비활성 시 호출부에서 계산 자체를 건너뛰게 한다.
		bool IsEnabled() const { return enabled; }

		/*
		직전 프레임에 stage된 행을 현재 확정된 BFM 모드로 완성해 기록한다.
		PredictManeuver::tick() 진입 직후에 호출한다.
		*/
		void FlushPending(BFM_Mode currentMode);

		/*
		현재 프레임의 각도 관련 값을 stage 한다.
		실제 기록은 다음 프레임의 FlushPending에서 이루어진다.
		*/
		void StageFrame(
			double time,
			float prevAngle,
			float currAngle,
			float rawDelta,
			float normalizedDelta,
			float avgDelta);

		// 남아 있는 stage 행을 기록하고 파일을 닫는다.
		void Close();

	private:
		PredictManeuverCsvLogger();
		~PredictManeuverCsvLogger();

		PredictManeuverCsvLogger(const PredictManeuverCsvLogger&) = delete;
		PredictManeuverCsvLogger& operator=(const PredictManeuverCsvLogger&) = delete;

		std::ofstream out;
		std::string runType;

		bool enabled = false;

		// stage된 행의 존재 여부와 값
		bool hasPending = false;
		long long pendingFrame = 0;
		double pendingTime = 0.0;
		float pendingPrevAngle = 0.0f;
		float pendingCurrAngle = 0.0f;
		float pendingRawDelta = 0.0f;
		float pendingNormalizedDelta = 0.0f;
		float pendingAvgDelta = 0.0f;

		// 모드 전환(previous != SCISSORS && current == SCISSORS) 판정용
		bool hasPrevMode = false;
		BFM_Mode prevMode = NONE;

		long long frameCounter = 0;
	};
}
