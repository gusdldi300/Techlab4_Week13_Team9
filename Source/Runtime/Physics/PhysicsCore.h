#pragma once

namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxPvd;
	class PxPvdTransport;
	class PxCpuDispatcher;
	class PxDefaultCpuDispatcher;
	class PxDefaultAllocator;
	class PxDefaultErrorCallback;
	class PxPvdSceneClient;
	class PxMaterial;
}

namespace nv { namespace cloth { class Factory; } }

class FPhysicsCore
{
	// 생성과 해제는 엔진 루프만 한다. 다른 곳에서 ShutdownPhysX를 부르면 살아 있는 Scene이 해제된 Physics를 쓰게 된다.
	friend class FEngineLoop;

public:
	static constexpr float DefaultFriction = 0.5f;
	static constexpr float DefaultRestitution = 0.1f;
	FPhysicsCore() = default;
	~FPhysicsCore() = default;
	FPhysicsCore(const FPhysicsCore&) = delete;
	FPhysicsCore& operator=(const FPhysicsCore&) = delete;

	static FPhysicsCore& Get() { static FPhysicsCore Instance; return Instance; }

	// 꺼낸 객체는 빌려 쓰기만 한다. release하거나 값을 바꾸지 않는다(기본 재질은 모든 바디가 공유한다).
	physx::PxPhysics* GetPhysics() { return gPhysics; };
	// PxDefaultCpuDispatcher → PxCpuDispatcher 변환은 상속 관계가 보여야 하므로 정의는 .cpp에 둔다.
	physx::PxCpuDispatcher* GetDispatcher();
	physx::PxMaterial* GetDefaultMaterial() { return gDefaultMaterial; }
	// 차량 컴포넌트는 이것이 false면 차를 만들지 않는다.
	bool IsVehicleSDKInitialized() const { return bVehicleSDKInitialized; }

	nv::cloth::Factory* GetClothFactory() { return gClothFactory; }

private:
	static bool InitPhysX();
	static void ShutdownPhysX();

	// 아래 객체는 밖에서 대입할 수 없도록 private으로 둔다. 접근은 위의 getter로만 한다.

	// PhysX의 최하위 기반 객체. 프로세스에 하나만 만든다.
	// 내부 메모리 할당과 에러 보고를 생성 시 넘긴 Allocator, ErrorCallback으로 전달한다.
	static physx::PxFoundation* gFoundation;

	//PhysX가 메모리가 필요할 때마다 호출하는 할당기 객체
	static physx::PxDefaultAllocator gAllocator;
	//PhysX가 경고나 에러를 알릴 때 호출하는 보고 객체
	static physx::PxDefaultErrorCallback gErrorCallback;

	//PhysX Visual Debugger, 엔진이 보낸 PhysX Scene 상태를 별도 프로그램에서 3D로 보여 주는 디버거
	//엔진 렌더링과 무관하게 실제 물리 모양, 접촉점, 관절을 볼 수 있다
	static physx::PxPvd* gPvd;
	//PVD 데이터를 실제로 내보내는 통로다. PxPvd가 무엇을 보낼지 정하고, Transport가 어디로 보낼지 정한다
	static physx::PxPvdTransport* gTransport;

	//PhysX 관련 객체들을 생성해주는 팩토리 객체 Shape, Material, Actor, Scene등이 있다.
	static physx::PxPhysics* gPhysics;
	//PhysX가 시뮬레이션 작업을 잘게 나눈 뒤, 그 작업들을 어느 스레드에서 실행할지 맡기는 인터페이스
	//PxCpuDispatcher 인터페이스에는 release()가 없으므로, 해제할 수 있도록 구체 타입으로 들고 있는다
	static physx::PxDefaultCpuDispatcher* gDispatcher;

	//물리 재질(UPhysicalMaterial)이 지정되지 않은 바디가 함께 쓰는 기본 재질. 마찰 0.5, 반발 0.1
	//재질은 여러 Scene과 바디가 공유할 수 있으므로 Physics와 같은 수명으로 Core가 소유한다
	static physx::PxMaterial* gDefaultMaterial;

	// PxInitVehicleSDK 성공 여부. PxCloseVehicleSDK는 성공했을 때만 부른다.
	static bool bVehicleSDKInitialized;

	// 천 객체(Fabric, Cloth, Solver)를 만드는 NvCloth 팩토리. CPU 솔버 전용이다. 프로세스에 하나만 만든다.
	static nv::cloth::Factory* gClothFactory;

};
