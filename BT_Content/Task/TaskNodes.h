#pragma once
#include "Task_LeadEntry.h" // Lead entry / follow-target init
#include "Task_Pure.h"
#include "Task_FollowTarget.h" // ���⸦ ����
#include "Task_ClimbToSafeAltitude.h" //1000ft �̻� �⵿

#include "Task_AggressiveOBFM.h" // OBFM 주 기동 (공격형 단일 노드, OBFM_Action 첫 자식)
#include "Task_AntiOvershoot.h" // OBFM�⵿ (Rule.xml 에서는 빠졌으나 등록은 유지)
#include "Task_CornerLeadPursuit.h" // OBFM 기동 (코너 속도 밴드 전용 lead pursuit)
#include "Task_LeadPursuit.h" // OBFM �⵿
#include "Task_EvasiveRollOrScissors.h" // DBFM ��� �⵿
#include "Task_CounterTurn.h" // DBFM ��� �⵿
#include "Task_RollReverseAttack.h" // DBFM �ݰ� �⵿
#include "Task_OneCircleAttack.h" // HABFM �⵿
#include "Task_TwoCircleAttack.h" // HABFM �⵿
#include "Task_ScissorBreakTurn.h" // Scissor �⵿
#include "Task_ScissorRollBack.h" // Scissor �⵿

#include "Task_MakeLOS.h"
#include "Task_CloseDistance.h"
#include "Task_MinimizeAngleOff.h"