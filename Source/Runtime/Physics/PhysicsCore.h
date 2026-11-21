#pragma once

namespace physx
{
	class PxFoundation;
	class PxPhysics;
	class PxPvd;
	class PxPvdTransport;
	class PxDefaultCpuDispatcher;
	class PxDefaultAllocator;
	class PxDefaultErrorCallback;
	class PxPvdSceneClient;
}



class FPhysicsCore
{
public:
	FPhysicsCore() = default;
	~FPhysicsCore() = default;
	FPhysicsCore(const FPhysicsCore&) = delete;
	FPhysicsCore& operator=(const FPhysicsCore&) = delete;


	static bool InitPhysX();

	static void ShutdownPhysX();

	static FPhysicsCore& Get() { static FPhysicsCore Instance; return Instance; }
	physx::PxPhysics* GetPhysics() { return gPhysics; };
	physx::PxCpuDispatcher* GetDispatcher() { return gDispatcher; };

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
	//hysX가 시뮬레이션 작업을 잘게 나눈 뒤, 그 작업들을 어느 스레드에서 실행할지 맡기는 인터페이스
	static physx::PxCpuDispatcher* gDispatcher;

};