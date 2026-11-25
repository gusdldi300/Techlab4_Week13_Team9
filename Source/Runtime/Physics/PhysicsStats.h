#pragma once
#include "Core/Stats/LightweightStats.h"

// stat physics가 표로 그리는 물리 구간 시간. 측정은 FPhysicsScene_PhysX의 StartFrame/EndFrame에서 한다.
namespace PhysicsStats
{
    // simulate 호출. 워커 스레드에 계산을 맡기고 바로 돌아오므로 보통 아주 작다.
    DECLARE_CYCLE_STAT("Physics: Simulate", STAT_PhysicsSimulate);
    // fetchResults(true). 게임 스레드가 물리 계산 완료를 기다린 시간이다.
    DECLARE_CYCLE_STAT("Physics: Fetch Results", STAT_PhysicsFetchResults);
    // 움직인 액터의 포즈를 컴포넌트에 반영한 시간
    DECLARE_CYCLE_STAT("Physics: Sync Components", STAT_PhysicsSyncComponents);
    // StartFrame과 EndFrame 전체
    DECLARE_CYCLE_STAT("Physics: Total", STAT_PhysicsTotal);
}
