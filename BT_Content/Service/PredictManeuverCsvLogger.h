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

기록 컬럼 (단위는 전부 degree / meter / second)
  runType          PM_CSV_RUNTYPE 값. before/after 비교의 그룹 키.
  episode          RunningTime 이 되감기면 +1 되는 경기 인덱스(0부터).
                   BT 쪽에는 경기 ID가 없어 이 로거가 파생시킨다.
  frame            프로세스 시작부터의 통산 프레임 번호.
  time             BB->RunningTime [sec]. 경기마다 0부터 다시 시작한다.
  prevAngle        avgDelta 창의 마지막 쌍에서 이전 Yaw [deg]
  currAngle        같은 쌍의 현재 Yaw [deg]
  rawDelta         currAngle - prevAngle, 보정 전 [deg]. ±358 같은 값이 나올 수 있다.
  normalizedDelta  BTAngle::SignedDeltaDeg 로 [-180,180) 접은 값 [deg]
  avgDelta         창(historySize-1개) 안 normalizedDelta 의 산술 평균 [deg]
  predictedTurn    이번 프레임의 LEFT / RIGHT / STRAIGHT 판정
  bfmMode          이 프레임 각도값에 대응하는 확정 BFM 모드
  scissorsEntered  비-SCISSORS -> SCISSORS 전환 프레임만 1. 체류 프레임은 0.
  distance_m       BB->Distance [m]
  ownAta_deg       BB->Los_Degree [deg]. 내 기수와 표적 LOS 사이의 각.
                   **부호 없는 0~180** 이다. 0 = 정조준. GeoMathUtil 의
                   부호 있는 ATA 와 크기는 같고 좌/우 구분이 없다.
  targetAa_deg     BB->MyAspectAngle_Degree [deg]. **부호 없는 0~180** 이며
                   AspectAngleUpdate 규약상 0 = 내가 표적의 기수 앞,
                   180 = 내가 표적의 6시. GeoMathUtil 의 AA 와 **반대 방향**이다
                   (그쪽은 0 이 표적의 6시). 두 값을 섞어 쓰지 말 것.
  angleOff_deg     BB->MyAngleOff_Degree [deg]. 기수 교차각.
  enemyInSight     BB->EnemyInSight (0/1)

BFM 모드는 Rule.xml 상 PredictManeuver보다 뒤에서 결정되므로,
한 프레임의 각도 값은 stage 해 두었다가 다음 tick 시작 시점에
확정된 BFM 모드와 함께 기록한다(1 프레임 지연 flush).

WEZ 상태는 BT 블랙보드에 없다(WEZ 판정은 Python 환경의 update_damage 가 한다).
따라서 이 CSV 에는 WEZ 컬럼이 없고, 분석 도구가 distance_m / ownAta_deg 와
--wez-* 옵션으로 derived 값을 만든다. 원본에 없는 값을 여기서 지어내지 않는다.
*/

#include "../BlackBoard/CPPBlackBoard.h"

#include <fstream>
#include <string>

namespace Action
{
	// 한 프레임에 기록할 값 묶음. 인자 개수가 늘어 순서를 헷갈리는 것을 막는다.
	struct PredictManeuverFrame
	{
		double time = 0.0;
		float prevAngle = 0.0f;
		float currAngle = 0.0f;
		float rawDelta = 0.0f;
		float normalizedDelta = 0.0f;
		float avgDelta = 0.0f;
		std::string predictedTurn;
		float distanceM = 0.0f;
		float ownAtaDeg = 0.0f;
		float targetAaDeg = 0.0f;
		float angleOffDeg = 0.0f;
		bool enemyInSight = false;
	};

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
		현재 프레임의 값을 stage 한다.
		실제 기록은 다음 프레임의 FlushPending에서 이루어진다.
		*/
		void StageFrame(const PredictManeuverFrame& frame);

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
		long long pendingEpisode = 0;
		PredictManeuverFrame pending;

		// 모드 전환(previous != SCISSORS && current == SCISSORS) 판정용
		bool hasPrevMode = false;
		BFM_Mode prevMode = NONE;

		// RunningTime 되감기로 경기 경계를 잡기 위한 상태
		bool hasPrevTime = false;
		double prevTime = 0.0;

		long long frameCounter = 0;
		long long episodeCounter = 0;
	};
}
