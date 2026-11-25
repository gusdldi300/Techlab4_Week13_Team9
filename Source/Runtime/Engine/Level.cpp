#include "EnginePCH.h"
#include "Level.h"
#include "World.h"

// delete Actor가 소멸자를 호출하려면 AActor의 완전한 정의가 필요하다.
// 전방 선언만으로는 C4150(소멸자 미호출)이 되어 액터가 GUObjectArray에 남는다.
#include "GameFramework/Actor.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PrimitiveComponent.h"

void ULevel::AddActor(AActor* Actor)
{
	if (!Actor)
		return;

	Actors.Add(Actor);
}

void ULevel::ClearActors()
{
	for (AActor* Actor : Actors)
	{
		if (!Actor)
			continue;

		delete Actor;
	}
	Actors.Reset();
}

void ULevel::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Actors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		if (Ar.IsLoading())
			Actors.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Actors[i];
			ActorArray.EnterElement() << Obj;
			Actors[i] = static_cast<AActor*>(Obj);
		}

		UObject* WorldObj = OwningWorld;
		Record << SA_VALUE("OwningWorld", WorldObj);
		OwningWorld = static_cast<UWorld*>(WorldObj);
		return;
	}

	if (Ar.IsSaving())
	{
		// 개수를 먼저 알아야 EnterArray를 할 수 있으므로 저장할 액터부터 고른다
		TArray<AActor*> SavedActors;
		for (AActor* Actor : Actors)
		{
			if (Actor && !Actor->HasAnyFlags(EObjectFlags::RF_Transient))
				SavedActors.Add(Actor);
		}

		int32 Num = SavedActors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		for (AActor* Actor : SavedActors)
		{
			FStructuredArchive::FRecord ActorRecord = ActorArray.EnterElement().EnterRecord();
			FString ClassName = Actor->GetClass()->Name;
			FString ActorName = Actor->GetName();
            FString ParentActorName, ParentComponentName;
            if (auto* Root = Actor->GetRootComponent())
                if (auto* Parent = Root->GetAttachParent())
                    if (AActor* Owner = Parent->GetOwner(); Owner && Owner != Actor && SavedActors.Find(Owner) != INDEX_NONE)
                    { ParentActorName = Owner->GetName(); ParentComponentName = Parent->GetName(); }
            ActorRecord << SA_VALUE("ParentActor", ParentActorName) << SA_VALUE("ParentComponent", ParentComponentName);
            ActorRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ActorName);
			Actor->Serialize(ActorRecord);
		}
		return;
	}

	// 불러오기: 월드를 비우는 건 호출하는 쪽 책임 (PIE 복제는 새 월드라 비울 필요가 없다)
	assert(OwningWorld);

    struct FPendingActorAttachment { AActor* Child; FString ParentActor; FString ParentComponent; };
    TArray<FPendingActorAttachment> Attachments;
    int32 Num = 0;
    FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
	for (int32 i = 0; i < Num; ++i)
	{
		FStructuredArchive::FRecord ActorRecord = ActorArray.EnterElement().EnterRecord();

		FString ClassName;
		FString ActorName;
		ActorRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ActorName);   // 없으면 빈 문자열

		UClass* Class = FindClass(ClassName);
		if (!Class || !Class->IsChildOf(AActor::StaticClass()))
		{
			KRS_LOG(Warning, "Load: unknown actor class '{}', skipped", ClassName);
			continue;
		}
		// 파일 안에 같은 이름이 또 있으면 그대로 생성할 때 앞서 만든 액터를 덮어쓰게 되므로 새 이름을 받는다
		FName Name = ActorName.empty() ? NAME_None : FName(ActorName);
		if (!Name.IsNone() && StaticFindObjectFast(nullptr, this, Name))
		{
			KRS_LOG(Warning, "Load: duplicate actor name '{}', renamed", ActorName);
			Name = NAME_None;
		}
		AActor* Actor = OwningWorld->SpawnActor(Class, Name);

		if (Actor)
		{
            Actor->Serialize(ActorRecord);
            FString ParentActor, ParentComponent;
            if (auto Slot = ActorRecord.TryEnterField("ParentActor", false)) Slot.GetValue() << ParentActor;
            if (auto Slot = ActorRecord.TryEnterField("ParentComponent", false)) Slot.GetValue() << ParentComponent;
            if (!ParentActor.empty()) Attachments.Add({Actor, ParentActor, ParentComponent});
        }
    }
    // All parents must exist before rebuilding inter-actor attachments.
    for (const auto& Pending : Attachments)
        for (AActor* ParentActor : Actors)
            if (ParentActor && ParentActor->GetName() == Pending.ParentActor)
            {
                auto* Parent = Cast<USceneComponent>(ParentActor->FindComponentByName(FName(Pending.ParentComponent)));
                if (Parent && Pending.Child->GetRootComponent())
                    Pending.Child->GetRootComponent()->SetupAttachment(Parent, EAttachmentRule::KeepRelative);
                break;
            }

	// SpawnActor가 원점에서 컴포넌트를 등록하면서 바디를 이미 만들었다. 바디는 그 뒤에 읽은 Transform을 따라가지 않으므로,
	// 저장된 위치·부착을 다 읽은 지금 다시 만든다. 다시 만들지 않으면 물리 Scene이 있는 월드(독립 실행 게임)에서
	// 모든 충돌체가 원점에 겹쳐 생기고, 시뮬레이션하는 바디는 그 위치에서 출발한다.
	// 에디터 월드는 물리 Scene이 없어 바디를 만들지 않으므로 아무 일도 하지 않는다.
	if (OwningWorld->GetPhysicsScene())
	{
		for (AActor* Actor : Actors)
		{
			if (!Actor) continue;
			for (UActorComponent* Component : Actor->GetComponents())
				if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
					Primitive->RecreatePhysicsState();
		}
	}
}
