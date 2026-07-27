#pragma once

/*
Task_LeadPursuit 실행 구간(진입 ~ 종료) 계측 전용 로거이다.
PredictManeuverCsvLogger와 같은 방식으로 환경변수로만 켜지며,
켜지지 않으면 모든 호출이 즉시 반환되어 시뮬레이션 동작에 영향이 없다.

왜 노드 밖에 두는가
-------------------
Task_LeadPursuit은 Timeout(Rule.xml:69)에 감싸인 상태로 halt될 수 있다.
halt는 타이머 스레드에서 들어오므로(decorators/timeout_node.cpp:70-79),
계측 상태를 노드 멤버에 두면 종료 사유 판정이 tick 경로와 얽힌다.
계측을 별도 객체로 분리해 노드에는 추종 로직만 남긴다.

활성화 조건
-----------
  LP_EXEC_CSV 에 출력 경로가 지정된 경우에만 CSV를 기록한다.
  LP_TICK_CSV 가 추가로 지정되면 tick 단위 상세 행도 남긴다(용량이 크다).
  LP_CONSOLE=0 이면 콘솔 출력을 끈다. 기본값은 켜짐이다.

환경변수
--------
  LP_EXEC_CSV   실행 단위 CSV 경로 (예: logs/leadpursuit_exec.csv)
  LP_TICK_CSV   tick 단위 CSV 경로 (선택)
  LP_CONSOLE    0 이면 콘솔 출력 비활성
  LP_RUNTYPE    runType 컬럼 값 (예: baseline, t3500). 미지정 시 "unknown"

콘솔 출력은 다음 5종으로만 제한한다.
  entry / status / exit / next_fallback_child / episode

종료 사유(exit_reason)
----------------------
노드가 스스로 판정할 수 있는 것은 SUCCESS, FAILURE, HALTED 뿐이다.
HALTED가 Timeout 만료 때문인지는 노드가 알 수 없으므로,
종료 행을 곧바로 쓰지 않고 pending 으로 들고 있다가
호출부가 Commit(timeoutFired)로 확정해 준 뒤 기록한다.
*/

#include "../BlackBoard/CPPBlackBoard.h"

#include <fstream>
#include <string>

namespace Action
{
	class LeadPursuitTelemetry
	{
	public:
		// 프로세스 전역 단일 인스턴스. 첫 호출 시 환경변수를 읽어 파일을 연다.
		static LeadPursuitTelemetry& Instance();

		bool IsEnabled() const { return enabled; }

		// --- 호출부(시뮬레이터/하네스)가 부르는 것 ---

		void BeginEpisode(int episodeId);

		// 에피소드 요약 콘솔 한 줄을 낸다.
		void EndEpisode();

		/*
		Fallback이 지금 어느 자식을 보고 있는지(current), 그리고 이 자식이 FAILURE로
		끝났을 때 이어서 평가될 자식이 무엇인지(next)를 기록용으로 알려준다.
		노드 자신은 부모 Fallback의 자식 목록을 알 수 없으므로 호출부가 지정한다.
		*/
		void SetFallbackChildren(const std::string& current, const std::string& next);

		/*
		pending 종료 행을 확정해 기록한다. Timeout 데코레이터를 tick 한 직후에 부른다.
		timeoutFired 가 true 이고 노드가 HALTED 로 끝났으면 TIMEOUT 으로 승격한다.
		pending 이 없으면 아무 일도 하지 않는다.
		*/
		void Commit(bool timeoutFired);

		/*
		Commit 전 대기 중인 종료 정보 조회.

		Fallback 은 어떤 자식이 SUCCESS 를 내면 haltChildren() 으로 모든 자식을
		IDLE 로 되돌린다(fallback_node.cpp:41-44). 그래서 fallback->executeTick()
		이 끝난 뒤에는 Timeout 노드의 FAILURE 를 더 이상 읽을 수 없다.
		호출부가 Timeout 발화를 판정하려면 이 값을 봐야 한다.
		*/
		bool HasPending() const { return hasPending; }
		const std::string& PendingExitReason() const { return pendingExitReason; }
		double PendingElapsedMs() const { return pendingElapsedMs; }
		double PendingEntryMs() const { return pendingEntryMs; }

		// --- Task_LeadPursuit 이 부르는 것 ---

		void OnEntry(const CPPBlackBoard& bb);

		void OnTick(const CPPBlackBoard& bb, double elapsedSec);

		// exitReason 은 "SUCCESS" | "FAILURE" | "HALTED"
		void OnExit(const char* nodeStatus, const char* exitReason,
			const CPPBlackBoard& bb, double elapsedSec);

		void Close();

	private:
		LeadPursuitTelemetry();
		~LeadPursuitTelemetry();

		LeadPursuitTelemetry(const LeadPursuitTelemetry&) = delete;
		LeadPursuitTelemetry& operator=(const LeadPursuitTelemetry&) = delete;

		// 직전 샘플과의 차분으로 접근률(closure rate)을 만든다. 양수면 접근 중이다.
		float ComputeClosureRate(const CPPBlackBoard& bb) const;

		void WriteTickRow(const CPPBlackBoard& bb, double elapsedSec, const char* phase);

		std::ofstream execOut;
		std::ofstream tickOut;

		std::string runType;
		std::string currentFallbackChild;
		std::string nextFallbackChild;

		bool enabled = false;
		bool tickEnabled = false;
		bool consoleEnabled = true;

		int episodeId = -1;

		// --- 진행 중인 실행 구간 ---
		bool  inExecution = false;
		double entryTimeSec = 0.0;
		float  entryRange = 0.0f;

		// closure rate 계산용 직전 샘플
		bool   hasPrevSample = false;
		double prevSampleTimeSec = 0.0;
		float  prevSampleRange = 0.0f;

		// --- 확정 대기 중인 종료 행 ---
		bool        hasPending = false;
		std::string pendingNodeStatus;
		std::string pendingExitReason;
		double      pendingEntryMs = 0.0;
		double      pendingExitMs = 0.0;
		double      pendingElapsedMs = 0.0;
		float       pendingRange = 0.0f;
		float       pendingClosureRate = 0.0f;
		float       pendingAngleOff = 0.0f;
		float       pendingAspectAngle = 0.0f;
		float       pendingOwnSpeed = 0.0f;
		float       pendingTargetSpeed = 0.0f;
		std::string pendingCurrentChild;
		std::string pendingNextChild;

		// --- 에피소드 집계 ---
		long long epExecs = 0;
		long long epSuccess = 0;
		long long epFailure = 0;
		long long epTimeout = 0;
		long long epHalted = 0;
	};
}
