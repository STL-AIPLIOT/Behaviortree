#pragma once
#include "DECO_BFMCheck.h"
#include "DECO_DistanceCheck.h"
#include "DECO_LOSCheck.h"
#include "DECO_AngleOffCheck.h"

#include "DECO_AltitudeCheck.h" //전투기의 고도가 1000ft 이상인지를 판단
#include "DECO_CounterAttackCheck.h" // DBFM 반격 관련 카운터 공격
#include "DECO_CanRetry.h" // CanRetry : next-tick bounded retry decorator
