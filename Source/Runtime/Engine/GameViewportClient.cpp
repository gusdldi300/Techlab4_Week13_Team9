#include "EnginePCH.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraOptics.h"
#include "Collision/Ray.h"
#include "Math/Vector4.h"
#include <cmath>
#include "Math/Frustum.h"
#include "Render/RenderCommand.h"
#include "UI/UserWidget.h"
#include "UI/UIRenderer.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"

FGameViewportClient::FGameViewportClient()
{
}

void FGameViewportClient::Init(FWorldContext& InWorldContext, UEngine* InEngine)
{
	SetWorld(InWorldContext.World());
	GameInstance = InWorldContext.OwningGameInstance;
	Engine = InEngine;
}

UWorld* FGameViewportClient::GetWorld() const
{
	return World;
}

void FGameViewportClient::MouseMove(FViewport* Viewport, int32 X, int32 Y)
{
	HandleUIMouseMove(X, Y);
}

bool FGameViewportClient::InputKey(FViewport* Viewport, int32 Key, bool bDown)
{
	if (HandleUIKey(Key, bDown))
		return true;

	if (World)
	{
		if (APlayerController* PC = World->GetPlayerController())
		{
			PC->InputKey(Key, bDown);
			return true;
		}
	}
	return false;
}

bool FGameViewportClient::InputAxis(FViewport* Viewport, int32 ControllerId, EGameInputAxis AxisKey, float Delta, float DeltaTime)
{
	if (HandleUIAxis(AxisKey, Delta))
		return true;

	if (World)
	{
		if (APlayerController* PC = World->GetPlayerController())
		{
			PC->InputAxis(AxisKey, Delta);
			return true;
		}
	}
	return false;
}

void FGameViewportClient::LostFocus()
{
	if (World)
	{
		if (APlayerController* PC = World->GetPlayerController())
			PC->ClearInputState();
	}
}

bool FGameViewportClient::ScreenToWorldRay(float X, float Y, FRay* OutRay)
{
	const FRect& DisplayRect = GetRect();
	if (!OutRay || !bHasRenderedView || DisplayRect.Width <= 0.0f || DisplayRect.Height <= 0.0f)
	{
		return false;
	}

	if (!(X >= 0.0f && Y >= 0.0f && X < DisplayRect.Width && Y < DisplayRect.Height))
	{
		return false;
	}

	// 표시 영역을 NDC로 변환하고 최종 렌더링 뷰로 역투영한다.
	const float NdcX = 2.0f * X / DisplayRect.Width - 1.0f;
	const float NdcY = 1.0f - 2.0f * Y / DisplayRect.Height;
	const FMatrix InverseViewProjection = LastRenderedView.ViewProjectionMatrix.Inverse();
	FVector4 NearPoint = FVector4(NdcX, NdcY, 0.0f, 1.0f) * InverseViewProjection;
	FVector4 InteriorPoint = FVector4(NdcX, NdcY, 0.5f, 1.0f) * InverseViewProjection;
	if (!std::isfinite(NearPoint.W) || !std::isfinite(InteriorPoint.W) || NearPoint.W == 0.0f || InteriorPoint.W == 0.0f)
	{
		return false;
	}

	NearPoint /= NearPoint.W;
	InteriorPoint /= InteriorPoint.W;
	const FVector NearWorld(NearPoint.X, NearPoint.Y, NearPoint.Z);
	const FVector Direction = FVector(InteriorPoint.X, InteriorPoint.Y, InteriorPoint.Z) - NearWorld;
	const float DirectionLengthSquared = Direction.SizeSquared();
	if (!std::isfinite(DirectionLengthSquared) || DirectionLengthSquared <= 0.0f)
	{
		return false;
	}

	// 원근은 카메라 위치, 직교는 픽셀별 Near 위치에서 출발한다.
	OutRay->Origin = LastRenderedView.bIsPerspective ? LastRenderedView.ViewLocation : NearWorld;
	OutRay->Direction = Direction / std::sqrt(DirectionLengthSquared);
	return true;
}

UCameraComponent* FGameViewportClient::FindPlayerCamera() const
{
	if (!World) return nullptr;
	if (APawn* Pawn = World->GetPlayerPawn())
	{
		if (ADefaultPawn* DefaultPawn = Cast<ADefaultPawn>(Pawn))
			if (UCameraComponent* Camera = DefaultPawn->GetCameraComponent()) return Camera;
		for (UActorComponent* Component : Pawn->GetComponents())
			if (UCameraComponent* Camera = Cast<UCameraComponent>(Component)) return Camera;
	}
	return nullptr;
}

void FGameViewportClient::SetWorld(UWorld* InWorld)
{
	if (World == InWorld) return;
	bHasRenderedView = false;
	LostFocus();
	RenderData.ResetScene();
	CameraComponent = nullptr;
	World = InWorld;
}

FSceneView FGameViewportClient::CalcSceneView(const FRect& InViewRect)
{
	FSceneView OutView;
	OutView.ViewIndex = 0;
	OutView.bGameView = true;
	OutView.ViewRect = InViewRect;

	Resize(
		static_cast<uint32>(std::max(InViewRect.Width, 1.0f)),
		static_cast<uint32>(std::max(InViewRect.Height, 1.0f))
	);

	const float Aspect = static_cast<float>(Width) / static_cast<float>(Height);

	if (!CameraComponent && World)
	{
		CameraComponent = FindPlayerCamera();

		if (!CameraComponent && World->GetMainCamera())
			CameraComponent = World->GetMainCamera()->GetCameraComponent();
	}

	if (World)
	{
		APlayerController* PC = World->GetPlayerController();
		APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;
		if (PCM && PCM->HasCameraCache())
		{
			FMinimalViewInfo POV = PCM->GetCameraCachePOV();
			POV.AspectRatio = Aspect;

			// 위치·회전·행렬
			OutView.ViewLocation = POV.Location;
			OutView.ViewRotation = POV.Rotation;
			OutView.ViewForward = POV.Rotation.Quaternion().GetForwardVector();
			OutView.ViewMatrix = POV.CalculateViewMatrix();
			OutView.ProjectionMatrix = POV.CalculateProjectionMatrix();
			OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;
			// 렌즈 값
			OutView.FieldOfView = POV.FOV;
			OutView.LensSettings = POV.LensSettings;
			OutView.DepthOfFieldSettings = POV.DepthOfFieldSettings;
			OutView.AspectRatio = Aspect;
			OutView.NearClip = POV.NearClip;
			OutView.FarClip = POV.FarClip;
			OutView.OrthoWidth = POV.OrthoWidth;
			OutView.bIsPerspective = !POV.bIsOrthogonal;
			OutView.bIsWireframe = false;

			// 프러스텀 후 반환
			OutView.Frustum = ExtractFrustumPlanes(OutView.ViewProjectionMatrix);
			OutView.FrustumCorners = GenerateFrustumCorners(OutView.ViewProjectionMatrix.Inverse());
			if (PCM->bEnableFading)
			{
				OutView.OverlayColor = PCM->FadeColor;
				OutView.OverlayColor.A = FMath::Clamp(PCM->FadeAmount, 0.0f, 1.0f);
			}
			OutView.LetterboxAmount = PCM->LetterboxAmount;
			OutView.LetterboxAspect = PCM->LetterboxAspect;
			OutView.VignetteIntensity = PCM->VignetteIntensity;

			return OutView;
		}
	}

	FMinimalViewInfo POV{};
	if (CameraComponent)
	{
		CameraComponent->SetAspectRatio(Aspect);
		CameraComponent->GetCameraView(0.0f, POV);
	}
	else
	{
		// 카메라가 없어도 기본 시점으로 행렬을 생성한다.
		POV.Location = FVector(-5.0f, -10.0f, 10.0f);
		POV.Rotation = FRotator(30.0f, 45.0f, 0.0f);
		POV.FarClip = 1000.0f;
	}

	POV.AspectRatio = Aspect;
	SanitizeDepthOfFieldSettings(POV.DepthOfFieldSettings);
	FinalizeCameraOptics(POV.LensSettings, POV.DepthOfFieldSettings, POV.FOV);
	OutView.ViewLocation = POV.Location;
	OutView.ViewRotation = POV.Rotation;
	OutView.ViewForward = POV.Rotation.Quaternion().GetForwardVector();
	OutView.ViewMatrix = POV.CalculateViewMatrix();
	OutView.ProjectionMatrix = POV.CalculateProjectionMatrix();
	OutView.ViewProjectionMatrix = OutView.ViewMatrix * OutView.ProjectionMatrix;
	OutView.FieldOfView = POV.FOV;
	OutView.AspectRatio = POV.AspectRatio;
	OutView.NearClip = POV.NearClip;
	OutView.FarClip = POV.FarClip;
	OutView.OrthoWidth = POV.OrthoWidth;
	OutView.bIsPerspective = !POV.bIsOrthogonal;
	OutView.bIsWireframe = false;
	OutView.LensSettings = POV.LensSettings;
	OutView.DepthOfFieldSettings = POV.DepthOfFieldSettings;

	OutView.Frustum = ExtractFrustumPlanes(OutView.ViewProjectionMatrix);
	OutView.FrustumCorners = GenerateFrustumCorners(OutView.ViewProjectionMatrix.Inverse());

	return OutView;
}

void FGameViewportClient::SetCameraComponent(UCameraComponent* InCameraComponent)
{
	CameraComponent = InCameraComponent;
}

bool FGameViewportClient::Draw(FRenderer* Renderer, FUIRenderer* UIRenderer)
{
	if (!World || !Renderer || !Width || !Height) return false;

	// 카메라를 뷰·투영 행렬로 바꾼다.
	const FRect ViewRect{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height) };
	const FSceneView SceneView = CalcSceneView(ViewRect);

	// 보이는 물체와 라이트를 모은 뒤 패스를 순서대로 실행해 ViewTargets에 그린다.
	FSceneRenderer SceneRenderer(World, SceneView, RenderData);
	SceneRenderer.InitViews(Renderer, EngineShowFlags);
	const bool bRendered = SceneRenderer.Render(Renderer, ViewTargets, EngineShowFlags,
		[this, UIRenderer, &SceneView](FScenePassCallbacks& Callbacks)
		{
			// 게임 UI는 씬 위에 그려야 하므로 그 시점에 불릴 함수로 등록한다.
			Callbacks.Overlays.GameUI = [this, UIRenderer]
				{
					if (!UIRenderer) return;
					PaintUI(UIDrawList);
					UIRenderer->Render(UIDrawList, Width, Height);
				};

			// 에디터가 등록해 둔 디버그 요소가 있으면 같이 등록한다.
			if (DebugDrawDelegate) DebugDrawDelegate(SceneView, Callbacks.Overlays);
		});
	bHasRenderedView = bRendered;
	if (bRendered)
		LastRenderedView = SceneView;
	return bRendered;
}

void FGameViewportClient::Resize(uint32 InWidth, uint32 InHeight)
{
	if (Width == InWidth && Height == InHeight && ViewTargets.IsValidFor(InWidth, InHeight))
	{
		return;
	}

	bHasRenderedView = false;
	Width = InWidth;
	Height = InHeight;

	if (Width == 0 || Height == 0)
	{
		ViewTargets.Reset();
		return;
	}

	ViewTargets.Resize(Width, Height);
}

void FGameViewportClient::Reset()
{
	bHasRenderedView = false;
	// PIE가 끝나면 화면에 남은 위젯을 정리한다.
	ClearViewportWidgets();

	RenderData.ResetScene();
	LostFocus();
	World = nullptr;
	GameInstance = nullptr;
	Engine = nullptr;
	CameraComponent = nullptr;

	ViewTargets.Reset();
	Width = 0;
	Height = 0;
}

void FGameViewportClient::AddViewportWidget(UUserWidget* Widget)
{
	if (!Widget || ViewportWidgets.Find(Widget) != INDEX_NONE) return;
	Widget->OwningViewport = this;

	// ZOrder가 같으면 나중에 추가한 것이 위에 온다.
	int32 Index = 0;
	while (Index < ViewportWidgets.Num() && ViewportWidgets[Index]->ZOrder <= Widget->ZOrder)
		++Index;
	ViewportWidgets.Insert(Widget, Index);
}

void FGameViewportClient::RemoveViewportWidget(UUserWidget* Widget)
{
	const int32 Index = ViewportWidgets.Find(Widget);
	if (Index == INDEX_NONE) return;
	ViewportWidgets.RemoveAt(Index);
	Widget->OwningViewport = nullptr;

	// 빠진 트리 안을 가리키고 있을 수 있으니 비운다.
	HoveredWidget = nullptr;
	PressedWidget = nullptr;
}

void FGameViewportClient::ClearViewportWidgets()
{
	const TArray<UUserWidget*> Widgets = ViewportWidgets;
	for (UUserWidget* Widget : Widgets)
		Widget->Destroy();
	ViewportWidgets.Empty();
	HoveredWidget = nullptr;
	PressedWidget = nullptr;
}

void FGameViewportClient::TickUI(float DeltaTime)
{
	// Tick 중에 위젯이 스스로 빠질 수 있어 복사본으로 돈다.
	const TArray<UUserWidget*> Widgets = ViewportWidgets;
	for (UUserWidget* Widget : Widgets)
		Widget->Tick(DeltaTime);
}

void FGameViewportClient::PaintUI(FUIDrawList& DrawList)
{
	DrawList.Reset();
	if (Width == 0 || Height == 0) return;

	const FUIGeometry Screen{ FUIRect{ 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height) },
		static_cast<float>(Height) / UIDesignHeight };
	for (UUserWidget* Widget : ViewportWidgets)
	{
		Widget->Arrange(Screen);
		Widget->Paint(DrawList, 1.0f);
	}
}

UWidget* FGameViewportClient::HitTestUI(float X, float Y) const
{
	for (auto It = ViewportWidgets.rbegin(); It != ViewportWidgets.rend(); ++It)
		if (UWidget* Hit = (*It)->HitTest(X, Y))
			return Hit;
	return nullptr;
}

bool FGameViewportClient::HandleUIMouseMove(int32 X, int32 Y)
{
	UIMouseX = X;
	UIMouseY = Y;

	UWidget* Hit = HitTestUI(static_cast<float>(X), static_cast<float>(Y));
	if (Hit != HoveredWidget)
	{
		if (HoveredWidget) HoveredWidget->OnMouseLeave();
		HoveredWidget = Hit;
		if (HoveredWidget) HoveredWidget->OnMouseEnter();
	}
	return false;   // 마우스 이동은 카메라도 써야 하므로 소비하지 않는다
}

bool FGameViewportClient::HandleUIKey(int32 Key, bool bDown)
{
	if (Key != static_cast<int32>(EKeyCode::LButton)) return false;

	const float X = static_cast<float>(UIMouseX);
	const float Y = static_cast<float>(UIMouseY);
	if (bDown)
	{
		UWidget* Hit = HitTestUI(X, Y);
		if (Hit && Hit->OnMouseButtonDown())
		{
			PressedWidget = Hit;
			return true;    // 버튼을 눌렀으면 게임으로 넘기지 않는다
		}
		return false;
	}

	if (!PressedWidget) return false;
	PressedWidget->OnMouseButtonUp(PressedWidget->GetCachedRect().Contains(X, Y));
	PressedWidget = nullptr;
	return true;
}

bool FGameViewportClient::HandleUIAxis(EGameInputAxis AxisKey, float Delta)
{
	return false;   // 스크롤 박스를 만들 때 휠을 여기서 처리한다
}
