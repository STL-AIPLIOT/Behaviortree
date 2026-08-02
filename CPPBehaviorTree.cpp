// Fill out your copyright notice in the Description page of Project Settings.


#include "CPPBehaviorTree.h"


/*
Task가 쓰로틀을 정하지 않았을 때 쓰는 기본값.
1.0은 이 값을 도입하기 전의 동작(Step에서 무조건 1.0으로 덮어쓰던 것)과 같다.
에너지 관리를 실험할 때 여기부터 낮춰 본다.
*/
static constexpr float DEFAULT_THROTTLE = 1.0f;


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

	tree.tickRoot(); //트리 작동

	VP = Vector3(BB->VP_Cartesian.X, BB->VP_Cartesian.Y, BB->VP_Cartesian.Z);

	Throttle = BB->Throttle;	// 쓰로틀 값 (Task가 정했으면 그 값, 아니면 DEFAULT_THROTTLE)
}

 void UCPPBehaviorTree::SetDeltaTime(double DT)
 {
	 BB->DeltaSecond = DT;
 }

