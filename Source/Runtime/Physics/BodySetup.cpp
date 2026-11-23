#include "EnginePCH.h"
#include "BodySetup.h"
#include "PhysicalMaterial.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include <fstream>
#include <limits>


float UBodySetup::CalculateMass() const
{
	if (PhysMaterial == nullptr) return AggGeom.GetVolume() * 1000.0f;

	return AggGeom.GetVolume() * PhysMaterial->GetDensity() * 1000.0f;
}

namespace
{
	// 파일 앞 4바이트. 다른 파일을 BodySetup으로 잘못 읽는 것을 막는다.
	constexpr uint32 BodySetupMagic = 0x59444F42; // "BODY"
	// 저장 형식을 바꾸면 올린다. 읽을 때 모르는 버전이면 실패로 처리한다.
	constexpr uint32 BodySetupVersion = 1;
	// 깨진 파일이 거대한 개수를 읽어 메모리를 다 쓰지 않도록 둔 상한
	constexpr int32 MaxElemsPerType = 4096;

	void SerializeVector(FArchive& Ar, FVector& V)
	{
		Ar << V.X << V.Y << V.Z;
	}

	void SerializeRotator(FArchive& Ar, FRotator& R)
	{
		Ar << R.Pitch << R.Yaw << R.Roll;
	}

	void SerializeElem(FArchive& Ar, FKSphereElem& E)
	{
		SerializeVector(Ar, E.Center);
		Ar << E.Radius << E.bContributeToMass;
	}

	void SerializeElem(FArchive& Ar, FKBoxElem& E)
	{
		SerializeVector(Ar, E.Center);
		SerializeRotator(Ar, E.Rotation);
		Ar << E.X << E.Y << E.Z << E.bContributeToMass;
	}

	void SerializeElem(FArchive& Ar, FKSphylElem& E)
	{
		SerializeVector(Ar, E.Center);
		SerializeRotator(Ar, E.Rotation);
		Ar << E.Radius << E.Length << E.bContributeToMass;
	}

	// 개수를 먼저 쓰고 원소를 이어서 쓴다. 읽을 때는 개수만큼 배열을 만든 뒤 채운다.
	template <typename TElem>
	void SerializeElems(FArchive& Ar, TArray<TElem>& Elems)
	{
		int32 Count = Elems.Num();
		Ar << Count;
		if (Ar.IsLoading())
		{
			if (Ar.IsError() || Count < 0 || Count > MaxElemsPerType)
			{
				Ar.SetError();
				return;
			}
			Elems.Empty();
			for (int32 i = 0; i < Count; ++i)
			{
				Elems.Add(TElem());
			}
		}
		for (TElem& Elem : Elems)
		{
			SerializeElem(Ar, Elem);
		}
	}
}

void UBodySetup::Serialize(FArchive& Ar)
{
	uint32 Magic = BodySetupMagic;
	uint32 Version = BodySetupVersion;
	Ar << Magic << Version;
	if (Ar.IsLoading() && (Magic != BodySetupMagic || Version != BodySetupVersion))
	{
		Ar.SetError();
		return;
	}

	uint8 Type = static_cast<uint8>(PhysicsType);
	Ar << Type;
	if (Ar.IsLoading())
	{
		if (Type > static_cast<uint8>(EPhysicsType::Simulated))
		{
			Ar.SetError();
			return;
		}
		PhysicsType = static_cast<EPhysicsType>(Type);
	}

	SerializeElems(Ar, AggGeom.SphereElems);
	SerializeElems(Ar, AggGeom.BoxElems);
	SerializeElems(Ar, AggGeom.SphylElems);
}

bool UBodySetup::LoadFromFile(const FString& Path)
{
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	if (!File)
	{
		return false;
	}
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		return false;
	}

	// 읽다가 실패하면 반쯤 채워진 모양이 남지 않도록 임시 객체에 먼저 읽는다.
	FKAggregateGeom PrevGeom = AggGeom;
	const EPhysicsType PrevType = PhysicsType;

	FMemoryReader Reader(Bytes, true);
	Serialize(Reader);
	if (Reader.IsError())
	{
		AggGeom = PrevGeom;
		PhysicsType = PrevType;
		KRS_LOG(Warning, "[Physics] Invalid body setup file: {}", Path);
		return false;
	}
	return true;
}

bool UBodySetup::SaveToFile(const FString& Path)
{
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Serialize(Writer);
	if (Writer.IsError())
	{
		return false;
	}

	std::ofstream File(Path, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		return false;
	}
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	return static_cast<bool>(File);
}
