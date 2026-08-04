#include "SelectTarget.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace Action
{
	PortsList SelectTarget::providedPorts()
	{
		return {
			InputPort<CPPBlackBoard*>("BB")
		};
	}



	NodeStatus SelectTarget::tick()
	{
		Optional<CPPBlackBoard*> BB = getInput<CPPBlackBoard*>("BB");

		/*
		[A] 방어 코드. 이전에는 getInput 반환값을 검사하지 않고 바로 (*BB) 를 썼고,
		표적을 못 정해도 무조건 SUCCESS 를 돌려주었다. 그래서 블랙보드를 못 받아도
		아무 신호 없이 다음 노드들이 0 값으로 계산을 이어갔다(무증상 실패).
		BB 를 못 받는 것은 배선 오류이므로 FAILURE 로 즉시 드러낸다.
		*/
		if (!BB || !(*BB))
		{
			std::cerr << "[SelectTarget] BB nullptr" << std::endl;
			return NodeStatus::FAILURE;
		}

		// [진단] BT_NODE_DIAG_LOG 가 설정된 경우에만 기록한다.
		{
			static std::ofstream diag = [] {
				std::ofstream o;
				const char* path = std::getenv("BT_NODE_DIAG_LOG");
				if (path && *path) { o.open(path, std::ios::out | std::ios::trunc); }
				return o;
			}();
			static long long n = 0;
			if (diag.is_open())
			{
				diag << "[SelectTarget " << n++ << "] BB=" << static_cast<void*>(*BB)
					<< " EnemySize=" << (*BB)->Enemy.size();
				if (!(*BB)->Enemy.empty())
				{
					diag << " Enemy0=(" << (*BB)->Enemy.at(0).Location.X << ","
						<< (*BB)->Enemy.at(0).Location.Y << ","
						<< (*BB)->Enemy.at(0).Location.Z << ")";
				}
				diag << std::endl;
			}
		}

		//std::cout << "Size : " << (*BB)->Enemy.size() << std::endl;

		//학생들은 1대1만 쓸꺼라 그냥 깡으로 타겟 지정
		if((*BB)->Enemy.size() > 0)
		{
			(*BB)->ACM = EF;
			
			(*BB)->TargetLocaion_Cartesian = (*BB)->Enemy.at(0).Location;
			{
				static std::ofstream d2 = [] {
					std::ofstream o;
					const char* path = std::getenv("BT_NODE_DIAG_LOG");
					if (path && *path) { o.open(path, std::ios::out | std::ios::app); }
					return o;
				}();
				if (d2.is_open())
				{
					d2 << "[SelectTarget]   wrote Target=("
						<< (*BB)->TargetLocaion_Cartesian.X << ","
						<< (*BB)->TargetLocaion_Cartesian.Y << ","
						<< (*BB)->TargetLocaion_Cartesian.Z << ")" << std::endl;
				}
			}
			(*BB)->TargetRotation_EDegree = (*BB)->Enemy.at(0).Rotation;
			(*BB)->TargetSpeed_MS = (*BB)->Enemy.at(0).Speed;

		}
		else
		{ 
			//std::cout << "타겟이 없음 or 타겟값이 제대로 안들어옴" << std::endl;
		}
				
		return NodeStatus::SUCCESS;
	}

}