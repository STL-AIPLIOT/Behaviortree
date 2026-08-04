// Fill out your copyright notice in the Description page of Project Settings.


#include "CPPBehaviorTree.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>


/*
Task가 쓰로틀을 정하지 않았을 때 쓰는 기본값.
1.0은 이 값을 도입하기 전의 동작(Step에서 무조건 1.0으로 덮어쓰던 것)과 같다.
에너지 관리를 실험할 때 여기부터 낮춰 본다.
*/
static constexpr float DEFAULT_THROTTLE = 1.0f;


/*
BT 진단 로그 (환경변수 BT_DIAG_LOG 가 있을 때만 동작)
-----------------------------------------------------
BT 노드가 std::cout 으로 찍는 진단은 Python(ctypes)으로 DLL 을 구동할 때
호출부의 stdout 리다이렉트로 수집되지 않는다(2026-08-04 실측: 3000 tick 에도 0줄,
SetStdHandle 로 핸들을 바꿔도 0바이트). 그래서 tick 이 실제로 도는지조차 확인할 수
없었다. 이 로거는 stdout 을 거치지 않고 파일에 직접 쓴다.

    BT_DIAG_LOG=logs/bt_diag.txt   # 지정하지 않으면 완전히 비활성
*/
namespace
{
	std::ofstream& BtDiagStream()
	{
		static std::ofstream stream = [] {
			std::ofstream out;
			const char* path = std::getenv("BT_DIAG_LOG");
			if (path && *path)
			{
				out.open(path, std::ios::out | std::ios::trunc);
			}
			return out;
		}();
		return stream;
	}

	bool BtDiagEnabled()
	{
		return BtDiagStream().is_open();
	}

	void BtDiag(const std::string& line)
	{
		if (!BtDiagEnabled())
		{
			return;
		}
		BtDiagStream() << line << "\n";
		BtDiagStream().flush();   // 크래시로 잃지 않도록 매번 flush
	}

	const char* StatusName(BT::NodeStatus s)
	{
		switch (s)
		{
		case BT::NodeStatus::IDLE:    return "IDLE";
		case BT::NodeStatus::RUNNING: return "RUNNING";
		case BT::NodeStatus::SUCCESS: return "SUCCESS";
		case BT::NodeStatus::FAILURE: return "FAILURE";
		}
		return "?";
	}
}


Vector3 UCPPBehaviorTree::LLAtoCartesian(Vector3 LLA, Vector3 BaseLLA)
{
	double eccentricitysquare, N, M;
	eccentricitysquare = 1.0 - pow(6356752.3142, 2) / pow(6378137.0, 2);
	N = 6378137.0 / sqrt(1.0 - eccentricitysquare * pow(sin(BaseLLA.X * PI / 180.0), 2)); // prime vertical radius of curvature
	M = 6378137.0 * (1.0 - eccentricitysquare) / pow(1 - eccentricitysquare * pow(sin(BaseLLA.X * PI / 180.0), 2), 3 / 2);

	double dlat, dlon;
	dlat = LLA.X - BaseLLA.X;
	dlon = LLA.Y - BaseLLA.Y;

	double dN, dE, dD;
	dN = (M + BaseLLA.Z) * dlat * PI / 180.0;
	dE = (N + BaseLLA.Z) * cos(BaseLLA.X * PI / 180.0) * dlon * PI / 180.0;
	dD = (LLA.Z - BaseLLA.Z);
	Vector3 res(dN, dE, dD);
	return res;
}

// Sets default values for this component's properties
UCPPBehaviorTree::UCPPBehaviorTree()
{

	f2m = 3.28084;
	EQ_R = 6.378137E+6;
	P_R = 6.3567523142E+6;
	fr = 298.257223563;
	Req = 6.378137E+6;
	d2r = 3.1415926535897931 / 180.0;
	m2f = 3.28084;


	elev0 = 0.2;
	aile0 = 0.0;
	eccen = 1.0 - P_R * P_R / (EQ_R * EQ_R);

	BB = new CPPBlackBoard();

	//std::cout << "Behavior Tree Version : 2022.07.11" << std::endl;
}


UCPPBehaviorTree::~UCPPBehaviorTree()
{
	delete BB;
}


void UCPPBehaviorTree::init()
{

	/*
	노드 입력 : 구현해둔 노드들을 Factory 객체에 입력해주는 과정
	
	새로 생성한 노드를 여기에 입력해주세요!!!!!!
	*/
	Factory.registerNodeType<Action::SelectTarget>("SelectTarget");
	Factory.registerNodeType<Action::DistanceUpdate>("DistanceUpdate");
	Factory.registerNodeType<Action::CheckSight>("CheckSight");
	Factory.registerNodeType<Action::AngleOffUpdate>("AngleOffUpdate");
	Factory.registerNodeType<Action::DirectionVectorUpdate>("DirectionVectorUpdate");
	Factory.registerNodeType<Action::AspectAngleUpdate>("AspectAngleUpdate");
	Factory.registerNodeType<Action::DECO_BFMCheck>("DECO_BFMCheck");
	Factory.registerNodeType<Action::DECO_DistanceCheck>("DECO_DistanceCheck");
	Factory.registerNodeType<Action::Task_LeadEntry>("Task_LeadEntry");
	Factory.registerNodeType<Action::Task_Pure>("Task_Pure");

	Factory.registerNodeType<Action::PredictManeuver>("PredictManeuver");
	Factory.registerNodeType<Action::EnergyCompare>("EnergyCompare");
	Factory.registerNodeType<Action::DECO_AltitudeCheck>("DECO_AltitudeCheck");
	Factory.registerNodeType<Action::Task_FollowTarget>("Task_FollowTarget");
	Factory.registerNodeType<Action::Task_ClimbToSafeAltitude>("Task_ClimbToSafeAltitude");

	Factory.registerNodeType<Action::SetBFMMode_OBFM>("SetBFMMode_OBFM");
	Factory.registerNodeType<Action::Task_AntiOvershoot>("Task_AntiOvershoot");
	Factory.registerNodeType<Action::Task_CornerLeadPursuit>("Task_CornerLeadPursuit");
	Factory.registerNodeType<Action::Task_LeadPursuit>("Task_LeadPursuit");

	Factory.registerNodeType<Action::SetBFMMode_DBFM>("SetBFMMode_DBFM");
	Factory.registerNodeType<Action::Task_EvasiveRollOrScissors>("Task_EvasiveRollOrScissors");
	Factory.registerNodeType<Action::Task_CounterTurn>("Task_CounterTurn");
	Factory.registerNodeType<Action::DECO_CounterAttackCheck>("DECO_CounterAttackCheck");
	Factory.registerNodeType<Action::DECO_CanRetry>("CanRetry");
	Factory.registerNodeType<Action::Task_RollReverseAttack>("Task_RollReverseAttack");


	Factory.registerNodeType<Action::SetBFMMode_HABFM>("SetBFMMode_HABFM");
	Factory.registerNodeType<Action::Task_OneCircleAttack>("Task_OneCircleAttack");
	Factory.registerNodeType<Action::Task_TwoCircleAttack>("Task_TwoCircleAttack");

	Factory.registerNodeType<Action::SetBFMMode_SCISSORS>("SetBFMMode_SCISSORS");
	Factory.registerNodeType<Action::Task_ScissorBreakTurn>("Task_ScissorBreakTurn");
	Factory.registerNodeType<Action::Task_ScissorRollBack>("Task_ScissorRollBack");

	Factory.registerNodeType<Action::Task_MakeLOS>("Task_MakeLOS");
	Factory.registerNodeType<Action::Task_MinimizeAngleOff>("Task_MinimizeAngleOff");
	Factory.registerNodeType<Action::Task_CloseDistance>("Task_CloseDistance");




	//파일로 트리 구조 정의
	//파일 이름은 본인의 팀이름으로 해주세요!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	tree = Factory.createTreeFromFile("./Rule.xml");

	// 트리가 실제로 만들어졌는지 (노드 개수 포함) 파일로 남긴다.
	if (BtDiagEnabled())
	{
		BtDiag("[init] createTreeFromFile OK, nodes=" +
			std::to_string(tree.nodes.size()) +
			", root=" + (tree.rootNode() ? tree.rootNode()->name() : std::string("(null)")));

		// 어떤 노드가 실제로 파싱됐는지 전부 남긴다.
		for (const auto& node : tree.nodes)
		{
			BtDiag(std::string("[init]   node: ") + node->name() +
				"  (type=" + node->registrationName() + ")");
		}

		// 실제 부모-자식 계층. tree.nodes 는 평면 목록이라 연결 상태를 보여주지 않는다.
		// 이 벤더 파서는 자식을 노드 '이름 문자열'로 연결하도록 개조돼 있어
		// (xml_parsing.cpp:599-604) 계층이 어긋날 수 있다.
		std::function<void(BT::TreeNode*, int)> dump = [&](BT::TreeNode* n, int depth)
		{
			if (!n) { return; }
			std::string pad(static_cast<size_t>(depth) * 2, ' ');
			if (auto* ctrl = dynamic_cast<BT::ControlNode*>(n))
			{
				BtDiag("[tree] " + pad + n->name() + " [Control, children=" +
					std::to_string(ctrl->children().size()) + "]");
				for (auto* c : ctrl->children()) { dump(c, depth + 1); }
			}
			else if (auto* deco = dynamic_cast<BT::DecoratorNode*>(n))
			{
				BtDiag("[tree] " + pad + n->name() + " [Decorator, child=" +
					(deco->child() ? deco->child()->name() : std::string("(null)")) + "]");
				dump(deco->child(), depth + 1);
			}
			else
			{
				BtDiag("[tree] " + pad + n->name() + " [Leaf]");
			}
		};
		dump(tree.rootNode(), 0);
	}
	
	//문자열로 트리 구조 정의
	//std::string XML = StrCat(xml_text1, xml_text2);
	////std::cout << XML << std::endl;
	//tree = Factory.createTreeFromText(XML);

	//블랙보드 연결 : 원래는 블랙보드 내에 있는 모든 변수를 하나하나 이런식으로 입력해줘야하는 미친 비효율을 보이는 방식이지만 커스텀 블랙보드를 만들어 해당 블랙보드를 입력시킴
	tree.rootBlackboard()->set<CPPBlackBoard*>("BB", BB);
	
}

StickValue UCPPBehaviorTree::Step(PlaneInfo MyInfo, int NumofOtherPlane, PlaneInfo* OthersInfo, Vector3 & VP, float & Throttle)
{
	/*LLA 좌표를 Cartesian 좌표로 변경
			
	굳이 리눅스의 기준 좌표 37, 127로 맞출 필요 없음
	*/
	Vector3 Mylocation_Cartesian = LLAtoCartesian(MyInfo.Location, Vector3(OriLAT, OriLOn, 0));

	//Cartesiam 좌표계로 위치 정보를 바꾼 내 비행기 정보
	PlaneInfo Myinfo;
	Myinfo.Location = Mylocation_Cartesian;
	Myinfo.Rotation = EulerAngle(MyInfo.Rotation.Yaw, MyInfo.Rotation.Pitch, MyInfo.Rotation.Roll);
	Myinfo.AngleAcceleration = MyInfo.AngleAcceleration;
	Myinfo.Speed = MyInfo.Speed;
	Myinfo.Team = MyInfo.Team;
	Myinfo.Resv0 = MyInfo.Resv0;		//ID
	Myinfo.Resv1 = MyInfo.Resv1;		//HP
	Myinfo.Resv2 = MyInfo.Resv2;		//OperationMode


	//HP가 0이하가 되면 자유 낙하하도록 설정
	if (Myinfo.Resv1 <= 0)
	{
		StickValue R;

		BB->VP_Cartesian = Vector3(BB->MyLocation_Cartesian.X, BB->MyLocation_Cartesian.Y, 0);
		R = Controller.GetStick(
			BB->MyLocation_Cartesian,
			Vector3(BB->MyRotation_EDegree.Roll*DEG2RAD,
				BB->MyRotation_EDegree.Pitch*DEG2RAD,
				BB->MyRotation_EDegree.Yaw*DEG2RAD),
			BB->VP_Cartesian);
		BB->Throttle = 0;
		R.RudderCMD = 100;

		std::cout << " HP : 0 !!!!!!!!!" << std::endl;
		return R;
	}

	//HP가 0이상일때
	else
	{
		//다른 비행기들 위치 좌표계 변환
		PlaneInfo others[4];
		for (int i = 0; i < NumofOtherPlane; i++)
		{
			Vector3 Enemylocation_Cartesian = LLAtoCartesian(OthersInfo[i].Location, Vector3(OriLAT, OriLOn, 0));
			others[i].Location = Enemylocation_Cartesian;
			others[i].Rotation = EulerAngle(OthersInfo[i].Rotation.Yaw, OthersInfo[i].Rotation.Pitch, OthersInfo[i].Rotation.Roll);
			others[i].Speed = OthersInfo[i].Speed;
			others[i].Team = OthersInfo[i].Team;
			others[i].Resv0 = OthersInfo[i].Resv0;
			others[i].Resv1 = OthersInfo[i].Resv1;
			others[i].Resv2 = OthersInfo[i].Resv2;
			
			
		}

		//블랙보드의 아군기, 적군기 List 초기화
		BB->Friendly.clear();
		BB->Enemy.clear();

		//블랙보드에 내 정보(위치, 자세, 속력, 팀) 업데이트
		BB->MyLocation_Cartesian = Mylocation_Cartesian;
		BB->MyRotation_EDegree = EulerAngle(Myinfo.Rotation.Yaw, Myinfo.Rotation.Pitch, Myinfo.Rotation.Roll);
		BB->MyAngleAcceleration = Myinfo.AngleAcceleration;
		BB->MySpeed_MS = Myinfo.Speed;
		BB->Team = (TeamColor)Myinfo.Team;

		//아군기 리스트에 내 정보 추가. Friendly의 index 0번은 무조건 나 자신
		BB->Friendly.push_back(Myinfo);

		//생존중인 비행기들의 적아 구분
		for (int i = 0; i < NumofOtherPlane; i++)
		{
			if (others[i].Resv1 > 0)
			{
				if (others[i].Team == Myinfo.Team)
				{
					BB->Friendly.push_back(others[i]);
				}
				else
				{
					BB->Enemy.push_back(others[i]);
				}
			}
			else
			{

			}
		}


		bool AimmingMode;

		StickValue R;

		//블랙보드에 입력된 정보를 바탕으로 비헤비어트리 Run
		//(예전에는 여기서 Throttle = 1.0 으로 덮어써서 BT가 정한 값이 버려졌다.
		// 지금은 RunCPPBT가 tick 전에 기본값을 넣고, Task가 덮어쓴 값을 그대로 내보낸다.)
		if (BtDiagEnabled())
		{
			std::string m = "[preTick] NumOther=" + std::to_string(NumofOtherPlane) +
				" EnemySize=" + std::to_string(BB->Enemy.size()) +
				" myLLA=(" + std::to_string(MyInfo.Location.X) + "," +
				std::to_string(MyInfo.Location.Y) + "," + std::to_string(MyInfo.Location.Z) + ")";
			if (NumofOtherPlane > 0)
			{
				m += " othLLA=(" + std::to_string(OthersInfo[0].Location.X) + "," +
					std::to_string(OthersInfo[0].Location.Y) + "," +
					std::to_string(OthersInfo[0].Location.Z) + ")" +
					" othCart=(" + std::to_string(others[0].Location.X) + "," +
					std::to_string(others[0].Location.Y) + "," +
					std::to_string(others[0].Location.Z) + ")";
			}
			if (!BB->Enemy.empty())
			{
				m += " Enemy0=(" + std::to_string(BB->Enemy[0].Location.X) + "," +
					std::to_string(BB->Enemy[0].Location.Y) + "," +
					std::to_string(BB->Enemy[0].Location.Z) + ")";
			}
			// 트리 노드가 실제로 받는 BB 와 이 객체의 BB 가 같은지 대조한다.
			{
				CPPBlackBoard* fromTree = nullptr;
				try { fromTree = tree.rootBlackboard()->get<CPPBlackBoard*>("BB"); }
				catch (...) { fromTree = reinterpret_cast<CPPBlackBoard*>(-1); }
				char buf[64];
				snprintf(buf, sizeof(buf), " thisBB=%p treeBB=%p",
					static_cast<void*>(BB), static_cast<void*>(fromTree));
				m += buf;
				m += (fromTree == BB) ? " SAME" : " *** DIFFERENT ***";
				if (fromTree && fromTree != reinterpret_cast<CPPBlackBoard*>(-1))
				{
					m += " treeBB.EnemySize=" + std::to_string(fromTree->Enemy.size());
				}
			}
			BtDiag(m);
		}

		RunCPPBT(VP, Throttle, AimmingMode);


		R = Controller.GetStick(
			BB->MyLocation_Cartesian,
			Vector3(BB->MyRotation_EDegree.Roll*DEG2RAD,
					BB->MyRotation_EDegree.Pitch*DEG2RAD,
					BB->MyRotation_EDegree.Yaw*DEG2RAD),
				VP);

 
		
		return R;
	}
}

Vector3 UCPPBehaviorTree::GetVP()
{
	Vector3 Vp = (*BB).VP_Cartesian;
	return Vp;
}


 void UCPPBehaviorTree::RunCPPBT(Vector3& VP, float& Throttle, bool& AimmingMode)
{

	BB->RunningTime += BB->DeltaSecond;	//시뮬레이선 타임에 따른 델타 타임 설정

	/*
	tick 전에 기본 쓰로틀을 넣어 둔다.
	대부분의 Task는 BB->Throttle을 건드리지 않으므로, 기본값이 없으면 블랙보드 초기값 0이
	그대로 나가 추력이 사라진다. Task가 값을 정하면 그 값이 그대로 살아남는다.
	*/
	BB->Throttle = DEFAULT_THROTTLE;

	/*
	[C-2 실험] tick 전에 트리 상태를 리셋한다.
	루트 Sequence 가 이전 tick 의 상태에 고착돼 자식을 돌지 않는다는 가설을 검증한다.
	BT_FORCE_HALT 를 설정했을 때만 동작한다(기본 동작 불변).
	*/
	{
		static const bool forceHalt = [] {
			const char* v = std::getenv("BT_FORCE_HALT");
			return v && *v && std::string(v) != "0";
		}();
		if (forceHalt && tree.rootNode())
		{
			tree.rootNode()->halt();
		}
	}

	const BT::NodeStatus rootStatus = tree.tickRoot(); //트리 작동

	if (BtDiagEnabled())
	{
		static long long diagTick = 0;
		BtDiag("[tick " + std::to_string(diagTick++) +
			"] t=" + std::to_string(BB->RunningTime) +
			" root=" + StatusName(rootStatus) +
			" BFM=" + std::to_string(static_cast<int>(BB->BFM)) +
			" enemies=" + std::to_string(BB->Enemy.size()) +
			// BFM 게이트가 읽는 값들. 어느 조건에서 막히는지 보려면 이게 필요하다.
			" sight=" + std::to_string(BB->EnemyInSight ? 1 : 0) +
			" D=" + std::to_string(BB->Distance) +
			" LOS=" + std::to_string(BB->Los_Degree) +
			" LOSt=" + std::to_string(BB->Los_Degree_Target) +
			" AA=" + std::to_string(BB->MyAspectAngle_Degree) +
			" AO=" + std::to_string(BB->MyAngleOff_Degree) +
			" E=" + std::to_string(BB->EnergyCompareResult) +
			" myXYZ=(" + std::to_string(BB->MyLocation_Cartesian.X) + "," +
			std::to_string(BB->MyLocation_Cartesian.Y) + "," +
			std::to_string(BB->MyLocation_Cartesian.Z) + ")" +
			" tgtXYZ=(" + std::to_string(BB->TargetLocaion_Cartesian.X) + "," +
			std::to_string(BB->TargetLocaion_Cartesian.Y) + "," +
			std::to_string(BB->TargetLocaion_Cartesian.Z) + ")" +
			" VP=(" + std::to_string(BB->VP_Cartesian.X) + "," +
			std::to_string(BB->VP_Cartesian.Y) + "," +
			std::to_string(BB->VP_Cartesian.Z) + ")" +
			" thr=" + std::to_string(BB->Throttle));
	}

	VP = Vector3(BB->VP_Cartesian.X, BB->VP_Cartesian.Y, BB->VP_Cartesian.Z);

	Throttle = BB->Throttle;	// 쓰로틀 값 (Task가 정했으면 그 값, 아니면 DEFAULT_THROTTLE)
}

 void UCPPBehaviorTree::SetDeltaTime(double DT)
 {
	 BB->DeltaSecond = DT;
 }

