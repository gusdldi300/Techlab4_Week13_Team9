#pragma once

#include "UObject/Class.h"
#include "../Components/SceneComponent.h"
#include "../Math/EngineMath.h"
#include "../Math/Rotator.h"
#include "Camera/CameraLensSettings.h"
#include "Camera/DepthOfFieldSettings.h"
#include "CameraComponent.generated.h"

struct FMinimalViewInfo;

UCLASS()
class UCameraComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCameraComponent() { PrimaryComponentTick.bCanEverTick = true; }   // 이동·회전 입력을 매 프레임 처리한다

	// 매 프레임 카메라 입력/이동 처리
	virtual void TickComponent(float DeltaTime) override;
	void PostEditChangeProperty(const FProperty& Property) override;

	// Camera 행렬 계산
	FMatrix GetViewMatrix() const;
	FMatrix GetPerspectiveMatrix() const;
	FMatrix GetOrthogonalMatrix() const;
	FMatrix GetProjectionMatrix() const;
	FMatrix GetViewProjectionMatrix() const;
	void GetCameraView(float DeltaTime, FMinimalViewInfo& OutPOV) const;

	// Picking
	FRay DeProjection(FVector2 MousePos, float ScreenW, float ScreenH);

	// Get & Set
	float GetFieldOfView() const { return FieldOfView; }
	void SetFieldOfView(float InFieldOfView) { FieldOfView = InFieldOfView; }
	float GetEffectiveFieldOfView() const;

	const FCameraLensSettings& GetLensSettings() const { return LensSettings; }
	const FDepthOfFieldSettings& GetDepthOfFieldSettings() const { return DepthOfFieldSettings; }
	void SetLensSettings(const FCameraLensSettings& InSettings) { LensSettings = InSettings; }
	void SetDepthOfFieldSettings(const FDepthOfFieldSettings& InSettings) { DepthOfFieldSettings = InSettings; }

	float GetAspectRatio() const { return AspectRatio; }
	void SetAspectRatio(float InAspectRatio) { AspectRatio = InAspectRatio; }

	float GetNearZ() const { return NearClipPlane; }
	void SetNearZ(float InNearZ) { NearClipPlane = InNearZ; }

	float GetFarZ() const { return FarClipPlane; }
	void SetFarZ(float InFarZ) { FarClipPlane = InFarZ; }

	bool GetIsOrthogonal() const { return bIsOrthogonal; }
	void SetIsOrthogonal(bool bInIsOrthographic) { bIsOrthogonal = bInIsOrthographic; }

	float GetOrthoWidth() const { return OrthoWidth; }
	void SetOrthoWidth(float InOrthoWidth) { OrthoWidth = InOrthoWidth; }

	// 마우스 조작 설정
	float GetMoveSpeed() const { return MoveSpeed; }
	void SetMoveSpeed(float InMoveSpeed) { MoveSpeed = InMoveSpeed; }

	float GetMouseSensitivity() const { return MouseSensitivity; }
	void SetMouseSensitivity(float InMouseSensitivity) { MouseSensitivity = InMouseSensitivity; }

	float GetWheelSpeed() const { return WheelSpeed; }
	void SetWheelSpeed(float InWheelSpeed) { WheelSpeed = InWheelSpeed; }
	// 외부 ViewportLayout가 View별 입력을 처리할 때 기본 카메라의 중복 입력 갱신을 끈다.

	void SetExternalInputManaged(bool bValue) { bExternalInputManaged = bValue; }
	bool IsExternalInputManaged() const { return bExternalInputManaged; }

	// 마우스 조작 업데이트
	void UpdateMovement(float DeltaTime); // 마우스 이동
	void UpdateRotation(float DeltaTime); // 마우스 우클릭 회전
	void UpdateZoom(float DeltaTime);     // 마우스 휠 줌

protected:
	UPROPERTY(EditAnywhere)
	float FieldOfView = 60.0f;

	UPROPERTY(EditAnywhere)
	FCameraLensSettings LensSettings;

	UPROPERTY(EditAnywhere)
	FDepthOfFieldSettings DepthOfFieldSettings;

	UPROPERTY(EditAnywhere)
	bool bAutoFocusEnabled = false;

	// 현재 게임 월드에서 이름으로 찾으므로 PIE 복제 전 액터를 참조하지 않는다.
	UPROPERTY(EditAnywhere)
	FString AutoFocusTargetActorName;

	UPROPERTY(EditAnywhere)
	FString AutoFocusTargetBoneName;

	// 본 위치에 더하는 월드 공간 오프셋(m).
	UPROPERTY(EditAnywhere)
	FVector AutoFocusTargetOffset = FVector::ZeroVector;

	// 본이 없으면 액터 위치에 이 월드 공간 오프셋(m)을 더한다.
	UPROPERTY(EditAnywhere)
	FVector AutoFocusFallbackOffset = FVector::ZeroVector;

	// 초점 전환 속도(1/s). 0이면 즉시 전환한다.
	UPROPERTY(EditAnywhere)
	float AutoFocusInterpSpeed = 8.0f;

	float AspectRatio = 16.0f / 9.0f;
	float NearClipPlane = 0.1f;
	float FarClipPlane = 10000.0f;

	bool bIsOrthogonal = false;
	float OrthoWidth = 16.0f;
	
	float MoveSpeed = 10.0f;
	float MouseSensitivity = 0.05f;
	float WheelSpeed = 0.1f;

	UPROPERTY(EditAnywhere)
	bool bExternalInputManaged = false;

private:
	bool ResolveAutoFocusTarget(FVector& OutLocation) const;

public:
	// UE와 같이 소유 Pawn의 Controller ControlRotation을 뷰 회전으로 쓴다 (부모/스프링 암 회전보다 우선)
	UPROPERTY(EditAnywhere)
	bool bUsePawnControlRotation = false;

};

