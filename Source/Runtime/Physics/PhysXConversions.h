#pragma once
// 엔진 타입과 PhysX 타입을 오가는 변환 함수 모음이다. (언리얼의 U2P / P2U와 같은 역할)
// PhysX 헤더를 끌어오므로 Physics 폴더의 .cpp에서만 include한다. 헤더에서 include하지 않는다.
//
// 엔진과 PhysX의 좌표 규약:
//  - 엔진은 Z가 위쪽이고 1 단위 = 1m다. PxTolerancesScale 기본값(length 1)과 중력 (0, 0, -9.81)이 그대로 맞는다.
//  - 그래서 성분을 그대로 복사한다. 축이나 단위를 바꾸게 되면 이 파일만 고치면 된다.

#include <PxPhysicsAPI.h>
#include "Math/Vector.h"
#include "Math/Quat.h"

inline physx::PxVec3 U2PVector(const FVector& V)
{
	return physx::PxVec3(V.X, V.Y, V.Z);
}

inline FVector P2UVector(const physx::PxVec3& V)
{
	return FVector(V.x, V.y, V.z);
}

inline physx::PxQuat U2PQuat(const FQuat& Q)
{
	return physx::PxQuat(Q.X, Q.Y, Q.Z, Q.W);
}

inline FQuat P2UQuat(const physx::PxQuat& Q)
{
	return FQuat(Q.x, Q.y, Q.z, Q.w);
}

// PxTransform에는 스케일이 없다. 스케일은 Shape 크기에 곱해서 반영한다.
inline physx::PxTransform U2PTransform(const FVector& Location, const FQuat& Rotation)
{
	return physx::PxTransform(U2PVector(Location), U2PQuat(Rotation).getNormalized());
}
