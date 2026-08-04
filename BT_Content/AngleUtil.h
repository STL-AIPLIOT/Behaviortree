#pragma once

/*
각도 wrap-around 공통 유틸리티.

배경
----
Yaw/Heading 처럼 주기가 360도인 값은 단순 뺄셈으로 차이를 구하면 안 된다.

    현재 -179도, 이전 +179도  ->  단순 차이 -358도
    실제 최소 회전량                    +2도

이 358도짜리 가짜 값이 PredictManeuver 의 avgDelta 를 ±180도 부근에서
급등시키고, 그 결과 회전 방향 판정(LEFT/RIGHT/STRAIGHT)이 반대로 뒤집힌다.

단위와 반환 범위
----------------
이 저장소의 각도는 전부 **degree** 다 (CPPBlackBoard 의 EulerAngle,
PlaneInfo::Rotation 주석 "Degree", GeoMathUtil 의 *_deg 값 모두 동일).
따라서 여기서도 degree 만 다룬다.

    WrapDeltaDeg / SignedDeltaDeg 의 반환 범위: [-180, 180)

경계 정책
---------
정확히 180도 차이는 좌회전과 우회전이 같은 크기라 부호를 정할 수 없다.
범위를 반열린 구간 [-180, 180) 으로 두었으므로 **항상 -180 으로 접는다**.
즉 SignedDeltaDeg(180, 0) == SignedDeltaDeg(0, 180) == -180.
이 값을 회전 방향 판정에 그대로 쓰면 두 경우 모두 RIGHT 로 읽히므로,
호출부가 180도 부근을 특별히 다뤄야 한다면 abs() 로 크기만 쓰는 편이 안전하다.

입력 제약
---------
입력은 0~360 으로 정규화되어 있을 필요가 없다. 음수 각도, 360도를 넘는 값,
여러 바퀴를 돈 값 모두 허용한다. NaN/inf 는 보정하지 않고 그대로 돌려준다
(0 으로 위장하면 이상 프레임이 정상 프레임과 구분되지 않는다).
*/

/*
A/B 비교용 스위치: PM_DISABLE_WRAP_FIX
--------------------------------------
wrap 보정의 효과를 측정하려면 "보정이 없던 상태"의 로그가 필요하다. 그런데
보정(normalizeAngleDelta)과 CSV 로거는 같은 커밋(0623e1b)에서 함께 들어왔다.
그 이전 커밋(e11bf5f)은 보정이 없는 대신 로거도 없어서, 옛 커밋을 체크아웃하는
방식으로는 'before' 로그를 만들 수 없다.

그래서 **보정만 끄는 컴파일 플래그**를 둔다. 이 플래그로 빌드한 DLL 은
e11bf5f 와 같은 계산

    sumDelta += prevHeadings[i] - prevHeadings[i - 1];      // 보정 없는 단순 뺄셈

을 하면서 CSV 로깅은 그대로 유지한다. 두 DLL 의 차이가 이 한 줄뿐이므로
공정한 A/B 가 된다.

    cl /DPM_DISABLE_WRAP_FIX ...
    tools\build_bt.ps1 -DisableWrapFix     # 'before' DLL

**이 플래그를 켠 빌드를 제출하거나 실제 교전에 쓰면 안 된다.** 켜면 컴파일 시
경고가 찍히도록 해 두었다.
*/
#ifdef PM_DISABLE_WRAP_FIX
#pragma message("경고: PM_DISABLE_WRAP_FIX 가 켜져 있다. 각도 wrap 보정이 꺼진 " \
                "A/B 비교 전용 빌드다. 제출/실전에 쓰지 말 것.")
#endif

#include <cmath>

namespace BTAngle
{
	/*
	이미 계산된 각도 차이를 [-180, 180) 으로 접는다.
	delta 가 유한하지 않으면(NaN/inf) 그대로 돌려준다.
	*/
	inline float WrapDeltaDeg(float delta)
	{
#ifdef PM_DISABLE_WRAP_FIX
		// A/B 비교 'before' 빌드: 보정하지 않고 그대로 돌려준다.
		// wrap 경계에서 ±358 같은 값이 그대로 avgDelta 에 들어간다.
		return delta;
#else
		if (!std::isfinite(delta))
		{
			return delta;
		}

		// fmod 는 피제수의 부호를 따르므로 음수 결과를 한 번 더 접어 올린다.
		float folded = std::fmod(delta + 180.0f, 360.0f);

		if (folded < 0.0f)
		{
			folded += 360.0f;
		}

		return folded - 180.0f;
#endif
	}

	/*
	두 각도 사이의 최소 부호 회전량. current - previous 를 [-180, 180) 으로 접는다.
	양수 = current 가 previous 보다 각도가 증가한 방향.
	*/
	inline float SignedDeltaDeg(float current, float previous)
	{
		return WrapDeltaDeg(current - previous);
	}

	/*
	각도 자체를 [-180, 180) 으로 정규화한다.
	WrapDeltaDeg 와 계산은 같지만 의미가 다르므로 이름을 나눠 둔다.
	*/
	inline float WrapAngleDeg(float angle)
	{
		return WrapDeltaDeg(angle);
	}
}
