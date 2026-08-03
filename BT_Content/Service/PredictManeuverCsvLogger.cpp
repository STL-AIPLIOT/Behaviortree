#include "PredictManeuverCsvLogger.h"

#include <cstdlib>
#include <iostream>
#include <limits>

#ifdef _WIN32
#include <direct.h>
#define PM_CSV_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define PM_CSV_MKDIR(path) mkdir(path, 0755)
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

				PM_CSV_MKDIR(dir.c_str());   // 이미 있으면 실패하지만 무시해도 된다.
			}
		}

		const char* BfmModeToString(BFM_Mode mode)
		{
			switch (mode)
			{
			case OBFM:      return "OBFM";
			case HABFM:     return "HABFM";
			case DBFM:      return "DBFM";
			case DETECTING: return "DETECTING";
			case SCISSORS:  return "SCISSORS";
			case NONE:      return "NONE";
			}

			return "UNKNOWN";
		}

		std::string ReadEnv(const char* key)
		{
			const char* value = std::getenv(key);

			return value ? std::string(value) : std::string();
		}
	}

	PredictManeuverCsvLogger& PredictManeuverCsvLogger::Instance()
	{
		// 지역 static이므로 프로세스 종료 시 소멸자가 호출되어 파일이 닫힌다.
		static PredictManeuverCsvLogger instance;

		return instance;
	}

	PredictManeuverCsvLogger::PredictManeuverCsvLogger()
	{
		const std::string path = ReadEnv("PM_CSV_LOG");

		// 경로가 지정되지 않으면 로깅을 완전히 비활성화한다.
		if (path.empty())
		{
			return;
		}

		EnsureParentDirectory(path);

		// trunc: 실행마다 파일을 새로 만들어 과거 로그가 섞이지 않게 한다.
		out.open(path.c_str(), std::ios::out | std::ios::trunc);

		if (!out.is_open())
		{
			std::cerr << "[PredictManeuverCsvLogger] 파일 열기 실패: " << path << "\n";
			return;
		}

		runType = ReadEnv("PM_CSV_RUNTYPE");

		if (runType.empty())
		{
			runType = "unknown";
		}

		// float/double 값을 문자열로 왕복해도 값이 보존되도록 자리수를 지정한다.
		out.precision(std::numeric_limits<double>::max_digits10);

		out << "runType,episode,frame,time,prevAngle,currAngle,rawDelta,"
			<< "normalizedDelta,avgDelta,predictedTurn,bfmMode,scissorsEntered,"
			<< "distance_m,ownAta_deg,targetAa_deg,angleOff_deg,enemyInSight\n";

		enabled = true;
	}

	PredictManeuverCsvLogger::~PredictManeuverCsvLogger()
	{
		Close();
	}

	void PredictManeuverCsvLogger::StageFrame(const PredictManeuverFrame& frame)
	{
		if (!enabled)
		{
			return;
		}

		/*
		BT 쪽에는 경기 ID가 없다. RunningTime 은 경기마다 0부터 다시 시작하므로
		시간이 뒤로 가면 새 경기로 본다(tools/extract_bfm_log.py 와 같은 규칙).
		부동소수 잡음으로 오탐하지 않도록 여유를 둔다.
		*/
		if (hasPrevTime && frame.time < prevTime - 1e-6)
		{
			++episodeCounter;
		}

		hasPrevTime = true;
		prevTime = frame.time;

		hasPending = true;
		pendingFrame = frameCounter++;
		pendingEpisode = episodeCounter;
		pending = frame;
	}

	void PredictManeuverCsvLogger::FlushPending(BFM_Mode currentMode)
	{
		if (!enabled || !hasPending)
		{
			return;
		}

		// 체류 프레임이 아니라 모드 전환만 1로 기록한다.
		const int scissorsEntered =
			(hasPrevMode && prevMode != SCISSORS && currentMode == SCISSORS) ? 1 : 0;

		out << runType << ','
			<< pendingEpisode << ','
			<< pendingFrame << ','
			<< pending.time << ','
			<< pending.prevAngle << ','
			<< pending.currAngle << ','
			<< pending.rawDelta << ','
			<< pending.normalizedDelta << ','
			<< pending.avgDelta << ','
			<< pending.predictedTurn << ','
			<< BfmModeToString(currentMode) << ','
			<< scissorsEntered << ','
			<< pending.distanceM << ','
			<< pending.ownAtaDeg << ','
			<< pending.targetAaDeg << ','
			<< pending.angleOffDeg << ','
			<< (pending.enemyInSight ? 1 : 0) << '\n';

		hasPrevMode = true;
		prevMode = currentMode;
		hasPending = false;
	}

	void PredictManeuverCsvLogger::Close()
	{
		if (!enabled)
		{
			return;
		}

		// 마지막 프레임은 뒤따르는 tick이 없으므로 직전 모드로 기록을 마무리한다.
		if (hasPending)
		{
			FlushPending(hasPrevMode ? prevMode : NONE);
		}

		out.flush();
		out.close();

		enabled = false;
	}
}
