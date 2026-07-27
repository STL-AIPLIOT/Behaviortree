#include "LeadPursuitTelemetry.h"

#include <cstdlib>
#include <iostream>
#include <limits>

#ifdef _WIN32
#include <direct.h>
#define LP_CSV_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define LP_CSV_MKDIR(path) mkdir(path, 0755)
#endif

namespace Action
{
	namespace
	{
		// 경로에 포함된 디렉터리를 앞에서부터 순서대로 생성한다(mkdir -p 동작).
		void EnsureParentDirectory(const std::string& filePath)
		{
			for (size_t i = 0; i < filePath.size(); ++i)
			{
				const char c = filePath[i];

				if (c != '/' && c != '\\')
				{
					continue;
				}

				if (i == 0)
				{
					continue;   // 루트 '/'는 만들 필요가 없다.
				}

				const std::string dir = filePath.substr(0, i);

				LP_CSV_MKDIR(dir.c_str());   // 이미 있으면 실패하지만 무시해도 된다.
			}
		}

		std::string ReadEnv(const char* key)
		{
			const char* value = std::getenv(key);

			return value ? std::string(value) : std::string();
		}
	}

	LeadPursuitTelemetry& LeadPursuitTelemetry::Instance()
	{
		// 지역 static이므로 프로세스 종료 시 소멸자가 호출되어 파일이 닫힌다.
		static LeadPursuitTelemetry instance;

		return instance;
	}

	LeadPursuitTelemetry::LeadPursuitTelemetry()
	{
		consoleEnabled = (ReadEnv("LP_CONSOLE") != "0");

		const std::string execPath = ReadEnv("LP_EXEC_CSV");

		// 경로가 지정되지 않으면 계측을 완전히 비활성화한다.
		if (execPath.empty())
		{
			return;
		}

		EnsureParentDirectory(execPath);

		// trunc: 실행마다 파일을 새로 만들어 과거 로그가 섞이지 않게 한다.
		execOut.open(execPath.c_str(), std::ios::out | std::ios::trunc);

		if (!execOut.is_open())
		{
			std::cerr << "[LeadPursuitTelemetry] 파일 열기 실패: " << execPath << "\n";
			return;
		}

		runType = ReadEnv("LP_RUNTYPE");

		if (runType.empty())
		{
			runType = "unknown";
		}

		execOut.precision(std::numeric_limits<double>::max_digits10);

		execOut << "runType,episode_id,entry_timestamp_ms,exit_timestamp_ms,elapsed_ms,"
			<< "node_status,exit_reason,range,closure_rate,angle_off,aspect_angle,"
			<< "own_speed,target_speed,current_fallback_child\n";

		enabled = true;

		const std::string tickPath = ReadEnv("LP_TICK_CSV");

		if (!tickPath.empty())
		{
			EnsureParentDirectory(tickPath);

			tickOut.open(tickPath.c_str(), std::ios::out | std::ios::trunc);

			if (tickOut.is_open())
			{
				tickOut.precision(std::numeric_limits<double>::max_digits10);

				tickOut << "runType,episode_id,phase,timestamp_ms,elapsed_ms,"
					<< "range,closure_rate,angle_off,aspect_angle,own_speed,target_speed\n";

				tickEnabled = true;
			}
		}
	}

	LeadPursuitTelemetry::~LeadPursuitTelemetry()
	{
		Close();
	}

	void LeadPursuitTelemetry::Close()
	{
		if (execOut.is_open())
		{
			execOut.flush();
			execOut.close();
		}

		if (tickOut.is_open())
		{
			tickOut.flush();
			tickOut.close();
		}

		enabled = false;
		tickEnabled = false;
	}

	float LeadPursuitTelemetry::ComputeClosureRate(const CPPBlackBoard& bb) const
	{
		if (!hasPrevSample)
		{
			return 0.0f;
		}

		const double dt = bb.RunningTime - prevSampleTimeSec;

		if (dt <= 0.0)
		{
			return 0.0f;
		}

		// 거리가 줄어드는 방향을 양수로 잡는다.
		return static_cast<float>((prevSampleRange - bb.Distance) / dt);
	}

	void LeadPursuitTelemetry::BeginEpisode(int id)
	{
		episodeId = id;

		epExecs = 0;
		epSuccess = 0;
		epFailure = 0;
		epTimeout = 0;
		epHalted = 0;

		inExecution = false;
		hasPrevSample = false;
		hasPending = false;
	}

	void LeadPursuitTelemetry::EndEpisode()
	{
		if (!consoleEnabled)
		{
			return;
		}

		std::cerr << "[LeadPursuit] episode ep=" << episodeId
			<< " execs=" << epExecs
			<< " success=" << epSuccess
			<< " failure=" << epFailure
			<< " timeout=" << epTimeout
			<< " halted=" << epHalted << "\n";
	}

	void LeadPursuitTelemetry::SetFallbackChildren(const std::string& current, const std::string& next)
	{
		currentFallbackChild = current;
		nextFallbackChild = next;
	}

	void LeadPursuitTelemetry::WriteTickRow(const CPPBlackBoard& bb, double elapsedSec, const char* phase)
	{
		if (!tickEnabled)
		{
			return;
		}

		tickOut << runType << ',' << episodeId << ',' << phase << ','
			<< (bb.RunningTime * 1000.0) << ',' << (elapsedSec * 1000.0) << ','
			<< bb.Distance << ',' << ComputeClosureRate(bb) << ','
			<< bb.MyAngleOff_Degree << ',' << bb.MyAspectAngle_Degree << ','
			<< bb.MySpeed_MS << ',' << bb.TargetSpeed_MS << '\n';
	}

	void LeadPursuitTelemetry::OnEntry(const CPPBlackBoard& bb)
	{
		if (!enabled)
		{
			return;
		}

		inExecution = true;
		entryTimeSec = bb.RunningTime;
		entryRange = bb.Distance;

		++epExecs;

		WriteTickRow(bb, 0.0, "ENTRY");

		if (consoleEnabled)
		{
			std::cerr << "[LeadPursuit] entry ep=" << episodeId
				<< " t=" << (bb.RunningTime * 1000.0) << "ms"
				<< " range=" << bb.Distance
				<< " angle_off=" << bb.MyAngleOff_Degree
				<< " aspect=" << bb.MyAspectAngle_Degree << "\n";

			// 상태 전이: IDLE -> RUNNING
			std::cerr << "[LeadPursuit] status ep=" << episodeId << " IDLE -> RUNNING\n";
		}

		hasPrevSample = true;
		prevSampleTimeSec = bb.RunningTime;
		prevSampleRange = bb.Distance;
	}

	void LeadPursuitTelemetry::OnTick(const CPPBlackBoard& bb, double elapsedSec)
	{
		if (!enabled || !inExecution)
		{
			return;
		}

		WriteTickRow(bb, elapsedSec, "RUNNING");

		hasPrevSample = true;
		prevSampleTimeSec = bb.RunningTime;
		prevSampleRange = bb.Distance;
	}

	void LeadPursuitTelemetry::OnExit(const char* nodeStatus, const char* exitReason,
		const CPPBlackBoard& bb, double elapsedSec)
	{
		if (!enabled || !inExecution)
		{
			return;
		}

		WriteTickRow(bb, elapsedSec, "EXIT");

		hasPending = true;
		pendingNodeStatus = nodeStatus;
		pendingExitReason = exitReason;
		pendingEntryMs = entryTimeSec * 1000.0;
		pendingExitMs = bb.RunningTime * 1000.0;
		pendingElapsedMs = elapsedSec * 1000.0;
		pendingRange = bb.Distance;
		pendingClosureRate = ComputeClosureRate(bb);
		pendingAngleOff = bb.MyAngleOff_Degree;
		pendingAspectAngle = bb.MyAspectAngle_Degree;
		pendingOwnSpeed = bb.MySpeed_MS;
		pendingTargetSpeed = bb.TargetSpeed_MS;
		pendingCurrentChild = currentFallbackChild;
		pendingNextChild = nextFallbackChild;

		inExecution = false;
		hasPrevSample = false;
	}

	void LeadPursuitTelemetry::Commit(bool timeoutFired)
	{
		if (!enabled || !hasPending)
		{
			return;
		}

		// 노드는 자기를 누가 halt 했는지 모른다. Timeout 만료가 확인된 경우에만 승격한다.
		if (timeoutFired && pendingExitReason == "HALTED")
		{
			pendingExitReason = "TIMEOUT";
		}

		if (pendingExitReason == "SUCCESS")
		{
			++epSuccess;
		}
		else if (pendingExitReason == "FAILURE")
		{
			++epFailure;
		}
		else if (pendingExitReason == "TIMEOUT")
		{
			++epTimeout;
		}
		else
		{
			++epHalted;
		}

		execOut << runType << ',' << episodeId << ','
			<< pendingEntryMs << ',' << pendingExitMs << ',' << pendingElapsedMs << ','
			<< pendingNodeStatus << ',' << pendingExitReason << ','
			<< pendingRange << ',' << pendingClosureRate << ','
			<< pendingAngleOff << ',' << pendingAspectAngle << ','
			<< pendingOwnSpeed << ',' << pendingTargetSpeed << ','
			<< pendingCurrentChild << '\n';

		if (consoleEnabled)
		{
			// 상태 전이 + 종료(또는 Timeout)
			std::cerr << "[LeadPursuit] status ep=" << episodeId
				<< " RUNNING -> " << pendingNodeStatus << "\n";

			std::cerr << "[LeadPursuit] exit ep=" << episodeId
				<< " reason=" << pendingExitReason
				<< " elapsed=" << pendingElapsedMs << "ms"
				<< " range=" << pendingRange << "\n";

			// Timeout/FAILURE 로 끝나면 Fallback이 다음 자식으로 넘어간다.
			if (pendingExitReason != "SUCCESS")
			{
				std::cerr << "[LeadPursuit] next_fallback_child ep=" << episodeId
					<< " from=" << pendingCurrentChild
					<< " to=" << pendingNextChild << "\n";
			}
		}

		hasPending = false;
	}
}
