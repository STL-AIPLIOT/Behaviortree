#pragma once

#include "../../behaviortree_cpp_v3/action_node.h"
#include "../BlackBoard/CPPBlackBoard.h"
#include <iostream>
#include <string>

namespace Action
{
    /*
    공격형 OBFM 단일 노드.

    설계 원칙
    ---------
    "거리·각도·에너지를 동시에 보존한다." 에너지를 extend(이탈)로 지키지 않고,
    각이 벌어졌을 때 추격 강도를 낮춰 in-fight 로 지킨다. 세 라운드 모두 완전 이탈은 없다
    (ALLOW_EXTEND = false). 공격형의 유일한 치명 실패는 오버슛 -> 역할 역전이므로,
    오버슛은 뒤로 빼기가 아니라 하이 요요(수직으로 빼서 공격위치 유지)로 처리한다.

    출력 계약
    ---------
    BB->VP_Cartesian 과 BB->Throttle(0~1) 만 쓴다. 다른 BB 필드는 읽기 전용이다.

    FAILURE 는 **퇴화 기하일 때만** 낸다 (BB nullptr / D 가 0 에 가까움 / 기수 벡터 미갱신).
    그 외에는 항상 SUCCESS 다.

    왜 노드 레벨 진입 게이트를 두지 않는가
    --------------------------------------
    브랜치 게이트가 이미 좁다. OBFM_Branch 는 SetBFMMode_OBFM 이 SUCCESS 일 때만 열리고,
    그 조건은 EnemyInSight && EnergyCompareResult > 0 && MyAspectAngle_Degree < 35 &&
    400 <= D <= 1500 이다 (SetBFMMode_OBFM.cpp:23-26). 즉 이 노드가 tick 되는 시점에
    상황은 이미 "시야 안 · 에너지 우세 · 적기 전방 섹터 · 중거리"로 제한돼 있고,
    노드가 다시 시야/거리/ATA 를 검사하는 것은 중복이다.

    대가는 분명하다. 이 노드가 OBFM_Action Fallback 의 첫 자식이므로, 퇴화 기하가 아닌 한
    뒤의 Task_CornerLeadPursuit / Task_LeadPursuit / Task_FollowTarget 은 실행되지 않는다.
    그것들은 통상 경로가 아니라 안전망으로 남기는 것이다 - 이 노드가 판단 근거를 잃었을 때
    (방향 벡터 미갱신 등) 트리가 무행동이 되지 않게 받아 주는 역할만 한다.

    각도 규약 주의
    --------------
    ATA 는 블랙보드에 없다. MyForwardVector 와 LOS(=Target-My) 로 노드 안에서 acos 로 만든다.
    AA 는 BB->MyAspectAngle_Degree 가 있지만 그 값은 **적기 기수 기준**이다
    (AspectAngleUpdate.cpp:23-29: 0 = 내가 적기 코앞, 180 = 내가 적기 6시).
    Docs 의 "AA 0 = 적기 6시" 와 반대 규약이므로 BB 값에 의존하지 않고 노드가 직접 계산한다.
    이 노드에서 aa_nose = 0 은 "적기가 나를 향하고 있다"(정면), 180 은 "내가 적기 6시"다.

    측정 (종료선)
    -------------
    STIL_AGGR_CSV 에 경로를 주면 tick 단위 CSV 를 남긴다. 미설정 시 완전 비활성이라
    시뮬레이션 동작에 영향이 없다(PredictManeuverCsvLogger / LeadPursuitTelemetry 와 같은 방식).
      핵심 지표 (a) gun_window = (WEZ_MIN_M < D < WEZ_MAX_M) && (ata <= DAMAGE_ATA_DEG) 인 tick 수
      핵심 지표 (b) 피격/역할역전 end_condition 이 함께 늘지 않았는가 (summary.json 쪽)
    gun_window 는 update_damage() 의 실제 판정(|ATA| <= wez.angle_deg/2 = 1 deg)과 같은 값으로만
    집계한다. 행동 티어의 1.5/2.5/3.0 deg 는 "행동 강도"일 뿐 데미지 판정이 아니다.
    */
    class Task_AggressiveOBFM : public BT::SyncActionNode
    {
    public:
        Task_AggressiveOBFM(const std::string& name, const BT::NodeConfiguration& config)
            : BT::SyncActionNode(name, config) {}

        static BT::PortsList providedPorts()
        {
            return { BT::InputPort<CPPBlackBoard*>("BB") };
        }

        BT::NodeStatus tick() override;

        // ================= 라운드 프로파일 =================
        /*
        라운드 신호는 코드·블랙보드·DLL export 어디에도 없다(2026-08-13 확인).
          - CPPBlackBoard.h 에 라운드/매치 인덱스 필드 없음
          - native_bt.py:106 CreateBehaviorTree(my_id, my_force_id) - 라운드 인자 없음
        그래서 컴파일 타임 상수로 둔다. 기본은 R12 다.
        재빌드 없이 3R 을 켜야 할 때만 환경변수 STIL_ROUND_PROFILE=R3 로 덮어쓴다
        (BT_RULE_XML / BT_DIAG_LOG 와 같은 방식이며, 미설정 시 동작 불변).
        */
        enum RoundProfile
        {
            // 1R, 2R: 에너지 여유가 얇아지면 티어 임계값을 줄여 일찍 추격 강도를 낮춘다.
            ROUND_R12,
            // 3R: 얇아져도 임계값을 줄이지 않는다(k=1 고정). 무승부는 지는 것과 같으므로
            //     계속 압박하고, 여유가 아주 얇은데 정면이면 정면교전으로 커밋한다.
            ROUND_R3
        };
        static constexpr RoundProfile ROUND_PROFILE = ROUND_R12;

        // 세 라운드 공통. 완전 이탈은 하지 않는다.
        static constexpr bool ALLOW_EXTEND = false;

        // ================= 튜닝 상수 (지정값) =================
        static constexpr float D_SWEET_AGGR = 300.0f;   // 공격형이 유지하려는 거리 [m]
        static constexpr float OVERSHOOT_CLOSURE = 18.0f;    // 하이 요요 발동 접근률 [m/s]
        static constexpr float OVERSHOOT_D = 350.0f;   // 하이 요요 발동 거리 [m]
        static constexpr float H_YOYO = 180.0f;   // 하이 요요 수직 오프셋 [m]
        static constexpr float K_MUZZLE = 300.0f;   // lead 비행시간 계산용 유효 탄속 가산 [m/s]
        // 적기 선회 안쪽으로 리드점을 미는 횡방향 바이어스 [m].
        // 방향은 BB->PredictedTurnDirection("LEFT"/"RIGHT")을 MyRightVector 에 투영해 정한다.
        // CONSERVE 에서는 쓰지 않는다 - 안쪽으로 당기는 것은 G 를 먹는 기동이라 보존과 반대다.
        static constexpr float TURN_IN_BIAS = 180.0f;
        static constexpr float ALLOUT_ATA = 1.5f;     // ALL_OUT 진입 ATA [deg]
        static constexpr float TIER_UP_ATA = 2.5f;     // 티어 상승 임계 [deg]
        static constexpr float TIER_DOWN_ATA = 3.0f;     // 티어 하강 임계 [deg]

        // ================= 파생 튜닝 상수 =================
        /*
        리드 비행시간의 상하한 [sec].
        t_lead = clamp(D / (MySpeed_MS + K_MUZZLE), MIN, MAX) 로 쓴다.

        상한 0.6 초가 핵심이다. D=1500 m / (250+300) m/s = 2.7 초까지 벌어질 수 있는데,
        그만큼 앞을 찍으면 리드점이 적기의 현재 선회 반경 밖으로 나가 버려 조준이 아니라
        엉뚱한 공간을 향하는 기동이 된다. 총알 비행시간에 해당하는 구간만 남긴다.
        */
        static constexpr float LEAD_TIME_MIN_SEC = 0.05f;
        static constexpr float LEAD_TIME_MAX_SEC = 0.6f;
        static constexpr float PURSUE_LEAD_GAIN = 1.3f;     // PURSUE 는 리드를 조금 더 앞에 찍는다
        static constexpr float CONSERVE_LAG_GAIN = 0.15f;    // 약한 lag = 거리의 15%
        static constexpr float CONSERVE_LAG_MAX_M = 250.0f;   // lag 상한. 이 이상은 사실상 extend 다
        static constexpr float HEADON_AA_DEG = 60.0f;    // R3 정면 커밋 판정 (적기 기수 기준)

        // ================= 에너지 =================
        /*
        왜 BB->EnergyCompareResult 를 쓰지 않는가
        ----------------------------------------
        그 값은 이 노드 안에서 **항상 +1 이라 정보량이 0 이다.** OBFM_Branch 는 Sequence 라
        SetBFMMode_OBFM 이 SUCCESS 일 때만 OBFM_Action 이 tick 되는데, 그 조건에
        EnergyCompareResult > 0 이 들어 있다(SetBFMMode_OBFM.cpp:19,26). 매 tick 재평가되므로
        이 노드가 도는 동안 에너지 열세는 발생할 수 없다.
        ecmp < 0 을 보는 코드는 전부 도달 불가능한 죽은 분기다.

        그래서 의미 있는 신호는 "우세냐 열세냐"가 아니라 **우세가 얼마나 얇으냐**다.
        ATA 를 BB 대신 직접 계산한 것과 같은 이유로, 삼치 요약 대신 원자료에서 연속량을 만든다.

            E      = V^2 + 2*g*h          (EnergyCompare.cpp:32-33 과 같은 식)
            margin = (E_me - E_tgt) / E_tgt

        margin 은 이 노드 안에서 항상 양수지만 0 에 가까울 수 있다. 그 얇기를 k 로 바꿔
        티어 임계값과 CONSERVE 강도에 함께 물린다.

            k = clamp(margin / E_MARGIN_FULL, E_TIER_K_MIN, 1.0)

        k=1 (여유 충분) 이면 임계값은 지정값 1.5/2.5/3.0 그대로다. margin 이 얇아질수록
        임계값이 최대 절반까지 줄어 같은 ATA 에서도 더 일찍 CONSERVE 로 내려간다 -
        대원칙의 "각이 벌어지면 추격 강도를 낮춰 in-fight 로 보존한다"를 에너지에 연동한 것이다.
        이탈은 여전히 하지 않는다(ALLOW_EXTEND = false).
        */
        static constexpr float E_G = 9.81f;    // EnergyCompare.cpp 와 같은 값
        static constexpr float E_MARGIN_FULL = 0.20f;   // 이 이상이면 감쇠 없음(k=1)
        static constexpr float E_TIER_K_MIN = 0.5f;    // 임계값 축소 하한
        static constexpr float E_MARGIN_THIN = 0.05f;   // R3 정면 커밋 판정 기준
        static constexpr float E_CONSERVE_LAG_MUL_MAX = 1.5f;    // k 최저일 때 lag 배수
        static constexpr float E_CONSERVE_THR_BIAS_MAX = 0.10f;   // k 최저일 때 스로틀 추가 감소

        // 스로틀
        static constexpr float THR_ALLOUT_BASE = 0.95f;
        static constexpr float THR_PURSUE_BASE = 0.85f;
        static constexpr float THR_CONSERVE_BASE = 0.55f;
        static constexpr float THR_YOYO_BASE = 0.45f;
        static constexpr float THR_HEADON_COMMIT = 0.90f;
        static constexpr float THR_CLOSURE_GAIN = 0.02f;    // 접근률 초과분 1 m/s 당 스로틀 감소
        static constexpr float THR_ALLOUT_GAIN = 0.01f;    // ALL_OUT 은 절반만 반응(각을 놓치지 않는다)
        static constexpr float CLOSURE_TARGET_MS = 10.0f;    // 스위트 거리 밖에서 허용하는 접근률

        // ================= WEZ (로깅 전용, 절대 넓히지 않는다) =================
        // update_damage() 는 |ATA| <= wez.angle_deg/2 = 1.0 deg 에서만 데미지를 준다.
        static constexpr float WEZ_MIN_M = 152.4f;   // 500 ft
        static constexpr float WEZ_MAX_M = 914.4f;   // 3000 ft
        static constexpr float DAMAGE_ATA_DEG = 1.0f;     // 실효 데미지 콘. 각도 epsilon 없음

    private:
        enum Tier
        {
            TIER_ALL_OUT,
            TIER_PURSUE,
            TIER_CONSERVE
        };

        static const char* TierName(Tier t);

        // 히스테리시스용. 버퍼 구간(2.5~3.0 deg)에서는 직전 티어를 유지해 채터링을 막는다.
        // 담당 상황을 벗어나(FAILURE) 다시 들어올 때는 보수적으로 시작한다.
        Tier prev_tier_ = TIER_CONSERVE;
    };
}
