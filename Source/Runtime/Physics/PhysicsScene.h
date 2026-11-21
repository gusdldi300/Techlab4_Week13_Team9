#pragma once


//서로다른 물리엔진에서 시뮬레이션 시키는 공간을 추상화한 클래스 physX는 PxScene, havok hknpWorld라는 이름으로 불리므로
//물리 엔진을 교체하더라도 유연하게 대응하기 위해 이 클래스를 만들었다.
class FPhysicsScene
{
public:
	FPhysicsScene() = default;
	virtual ~FPhysicsScene() = default;
	FPhysicsScene(const FPhysicsScene&) = delete;
	FPhysicsScene& operator=(const FPhysicsScene&) = delete;

	virtual void CreatePhysicsScene();

private:



};