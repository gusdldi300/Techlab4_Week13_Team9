#pragma once
#include "Physics/PhysicsHandles.h"
#include "Containers/Array.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

class FPhysicsScene;

namespace nv
{
	namespace cloth
	{
		class Fabric;
	}
}

// 천 하나를 만들 때 필요한 정보. 정점은 천의 로컬 공간(컴포넌트 기준, 스케일 제외)이다.
struct FClothInstanceInitParams
{
	TArray<FVector> LocalPositions;
	// 정점마다 역질량. 0이면 그 정점은 로컬 공간에 고정된다(깃대, 커튼 봉).
	TArray<float> InvMasses;
	// 삼각형 3개씩 묶은 정점 인덱스
	TArray<uint32> Indices;

	// 천 로컬 공간의 시작 위치와 회전(월드 기준)
	FVector WorldLocation;
	FQuat   WorldRotation;

	FVector Gravity = FVector(0.0f, 0.0f, -9.81f);
	float VerticalStiffness = 1.0f;   // 세로 구속(eVERTICAL, 중력 방향). 낮을수록 아래로 늘어난다
	float HorizontalStiffness = 1.0f; // 가로 구속(eHORIZONTAL, 중력에 수직). 낮을수록 옆으로 벌어진다
	float ShearStiffness = 1.0f;     // 대각선 구속(eSHEARING). 낮을수록 마름모로 쉽게 찌그러진다
	float BendStiffness = 0.5f;      // 접힘 구속. 낮을수록 실크, 높을수록 가죽처럼 보인다
	float Damping = 0.1f;            // 0이면 감쇠 없음, 1이면 매 스텝 속도 0
	float SolverFrequency = 120.0f;  // 1초당 구속 반복 횟수
};

// 천 하나의 NvCloth 객체(Fabric, Cloth)를 소유하고 Scene의 Solver에 넣고 빼는 래퍼다.
// UClothComponent가 값 멤버로 가진다. NvCloth 호출은 이 클래스의 .cpp에만 있다.
class FClothInstance
{
public:
	FClothInstance() = default;
	~FClothInstance();
	// Cloth와 Fabric을 소유하므로 복사하면 두 인스턴스가 같은 객체를 지우게 된다.
	FClothInstance(const FClothInstance&) = delete;
	FClothInstance& operator=(const FClothInstance&) = delete;

	// 쿠킹 → Cloth 생성 → Phase 설정 → Scene에 추가. 이미 있으면 먼저 Term한다.
	bool Init(const FClothInstanceInitParams& Params, FPhysicsScene* InScene);
	// Scene에서 빼고 Cloth와 Fabric을 해제한다. 여러 번 불러도 된다.
	void Term();

	bool IsValid() const { return Handle.IsValid(); }

	// 천 로컬 공간을 옮긴다. 다음 스텝에서 이동량만큼 관성(끌려가는 힘)이 생긴다.
	void SetWorldTransform(const FVector& Location, const FQuat& Rotation);
	// 시뮬레이션된 입자 위치(로컬 공간)를 읽는다. 정점 순서는 Init에 넘긴 것과 같다.
	void GetLocalPositions(TArray<FVector>& OutPositions) const;
	// 바람(월드 기준 m/s)과 공기 계수. Drag·Lift가 0이면 바람이 있어도 천에 힘이 생기지 않는다.
	// Drag는 바람 방향으로 미는 힘, Lift는 바람에 비스듬한 면을 옆으로 띄우는 힘이다(펄럭임을 만든다).
	void SetWind(const FVector& Velocity, float DragCoefficient, float LiftCoefficient);

private:
	FClothHandle Handle;
	// 쿠킹한 구속 정보. 참조 카운트로 관리하므로 delete가 아니라 decRefCount로 놓는다.
	nv::cloth::Fabric* Fabric = nullptr;
	// 넣은 Scene. 뺄 때 같은 Scene에서 빼야 하므로 기억한다. 수명은 UWorld가 관리한다.
	FPhysicsScene* Scene = nullptr;
};
