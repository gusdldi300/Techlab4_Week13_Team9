#include "EnginePCH.h"
#include "StatOverlay.h"

#include "EngineStatics.h"
#include "Render/PostProcess/DepthOfFieldStats.h"

#include <cctype>
#include <cmath>
#include <format>
#include <psapi.h>

#pragma comment(lib, "psapi")

namespace
{
	// 콘솔 입력은 대소문자를 구분하지 않는다. UE도 "STAT FPS"와 "stat fps"를 같게 다룬다.
	bool EqualsIgnoreCase(const FString& A, const char* B)
	{
		uint64 Index = 0;
		for (; Index < A.size() && B[Index] != '\0'; ++Index)
		{
			const int Left = std::tolower(static_cast<unsigned char>(A[Index]));
			const int Right = std::tolower(static_cast<unsigned char>(B[Index]));
			if (Left != Right)
				return false;
		}
		return Index == A.size() && B[Index] == '\0';
	}

	// 공백 구분 토큰으로 나눈다. 연속 공백과 앞뒤 공백은 무시한다.
	void Tokenize(const FString& Line, TArray<FString>& OutTokens)
	{
		OutTokens.Reset();
		uint64 Cursor = 0;
		while (Cursor < Line.size())
		{
			while (Cursor < Line.size() && std::isspace(static_cast<unsigned char>(Line[Cursor])))
				++Cursor;
			const uint64 Start = Cursor;
			while (Cursor < Line.size() && !std::isspace(static_cast<unsigned char>(Line[Cursor])))
				++Cursor;
			if (Cursor > Start)
				OutTokens.Add(Line.substr(Start, Cursor - Start));
		}
	}
}

// 샘플 구간이 찰 때만 표시 수치를 바꿔 숫자가 매 프레임 튀지 않게 한다.
void FStatOverlay::Tick(const float DeltaTime)
{
	AccumulatedTime += DeltaTime;
	++AccumulatedFrames;

	if (AccumulatedTime < SampleInterval || AccumulatedFrames <= 0)
		return;

	DisplayFPS = static_cast<float>(AccumulatedFrames) / AccumulatedTime;
	DisplayFrameTimeMs = (AccumulatedTime / static_cast<float>(AccumulatedFrames)) * 1000.0f;

	AccumulatedTime = 0.0f;
	AccumulatedFrames = 0;
}

// stat 명령만 소비하고 나머지는 호출자에게 되돌린다.
bool FStatOverlay::ExecCommand(const FString& CommandLine, FString& OutMessage)
{
	TArray<FString> Tokens;
	Tokenize(CommandLine, Tokens);

	if (Tokens.Num() == 0 || !EqualsIgnoreCase(Tokens[0], "stat"))
		return false;

	if (Tokens.Num() < 2)
	{
		OutMessage = "Usage: stat <fps|memory|profile|light|decal|skinning|particle|dof|all|none>";
		return true;
	}

	const FString& Arg = Tokens[1];

	if (EqualsIgnoreCase(Arg, "none"))
	{
		Flags = EStatFlags::None;
		OutMessage = "stat none: all overlays disabled";
		return true;
	}

	if (EqualsIgnoreCase(Arg, "all"))
	{
		Flags = EStatFlags::FPS | EStatFlags::Memory | EStatFlags::Profile
			| EStatFlags::Light | EStatFlags::Decal | EStatFlags::DepthOfField;
		OutMessage = "stat all: all overlays enabled";
		return true;
	}

	EStatFlags Target = EStatFlags::None;
	if (EqualsIgnoreCase(Arg, "fps"))
		Target = EStatFlags::FPS;
	else if (EqualsIgnoreCase(Arg, "memory"))
		Target = EStatFlags::Memory;
	else if (EqualsIgnoreCase(Arg, "profile"))
		Target = EStatFlags::Profile;
	else if (EqualsIgnoreCase(Arg, "light"))
		Target = EStatFlags::Light;
	else if (EqualsIgnoreCase(Arg, "decal"))
		Target = EStatFlags::Decal;
	else if (EqualsIgnoreCase(Arg, "skinning"))
		Target = EStatFlags::Skinning;
	else if (EqualsIgnoreCase(Arg, "particle"))
		Target = EStatFlags::Particle;
	else if (EqualsIgnoreCase(Arg, "dof"))
		Target = EStatFlags::DepthOfField;


	if (Target == EStatFlags::None)
	{
		OutMessage = std::format("stat: unknown stat '{}'. Available: fps, memory, profile, light, decal, skinning, particle, dof, all, none", Arg);
		return true;
	}

	// UE와 같이 같은 명령을 다시 입력하면 해당 항목만 꺼진다.
	const bool bEnable = !HasFlag(Flags, Target);
	if (bEnable)
		Flags |= Target;
	else
		Flags &= ~Target;

	OutMessage = std::format("stat {}: {}", Arg, bEnable ? "on" : "off");
	return true;
}

TArray<FString> FStatOverlay::GetDepthOfFieldLines(const FDepthOfFieldStats& Stats)
{
	TArray<FString> Lines;
	Lines.Add(std::format("  State          {}", Stats.State));
	if (!Stats.bHasData)
		return Lines;

	const FDepthOfFieldSettings& Settings = Stats.Settings;
	Lines.Add(std::format("  Settings       {}", Stats.bOverrideCamera ? "Override" : "Camera"));
	Lines.Add(std::format("  F-Stop         f/{:.2f}", Settings.FStop));
	Lines.Add(std::format("  Max Aperture   f/{:.2f}", Settings.MinFStop));
	Lines.Add(std::format("  Focus          {:.3f} m", Settings.FocalDistanceMeters));
	Lines.Add(std::format("  Focal Length   {:.2f} mm", Stats.Lens.FocalLengthMm));

	// 1/f = 1/u + 1/v 에서 v = fu / (u - f). 초점면에 맞춘 센서 위치다.
	const double FocalLengthMm = Stats.Lens.FocalLengthMm;
	const double FocusDistanceMm = Settings.FocalDistanceMeters * 1000.0;
	if (std::isfinite(FocalLengthMm) && std::isfinite(FocusDistanceMm)
		&& FocalLengthMm > 0.0 && FocusDistanceMm > FocalLengthMm)
	{
		const double ImageDistanceMm = FocalLengthMm * FocusDistanceMm
			/ (FocusDistanceMm - FocalLengthMm);
		Lines.Add(std::format("  Image Distance {:.3f} mm", ImageDistanceMm));
	}
	else
	{
		Lines.Add("  Image Distance N/A");
	}

	Lines.Add(std::format("  Sensor Height  {:.2f} mm", Stats.Lens.SensorHeightMm));
	Lines.Add(std::format("  Aperture       {} blades", Settings.ApertureBladeCount));
	if (Settings.DepthBlurKmFor50Percent > 0.0f
		&& Settings.DepthBlurRadiusFractionOfHeight > 0.0f)
	{
		Lines.Add(std::format("  Depth Blur     {:.3f} km at 50%", Settings.DepthBlurKmFor50Percent));
		Lines.Add(std::format("  Blur Radius    {:.2f}% of height",
			Settings.DepthBlurRadiusFractionOfHeight * 100.0f));
	}
	else
	{
		Lines.Add("  Depth Blur     OFF");
	}
	Lines.Add(std::format("  Radius Limit   {:.0f} px", Stats.MaximumRadiusFullResolutionPixels));

	const char* DebugView = "None";
	switch (Stats.DebugView)
	{
	case EDepthOfFieldDebugView::SignedCoC: DebugView = "Depth of Field Layers"; break;
	case EDepthOfFieldDebugView::LensRadius: DebugView = "Lens Radius"; break;
	case EDepthOfFieldDebugView::AdditionalBlur: DebugView = "Additional Blur"; break;
	case EDepthOfFieldDebugView::EffectiveRadius: DebugView = "Effective Radius"; break;
	default: break;
	}
	Lines.Add(std::format("  Debug View     {}", DebugView));
	Lines.Add(std::format("  Focus Plane    {}", Stats.bFocusPlaneDrawn ? "ON" : "OFF"));
	return Lines;
}

uint64 FStatOverlay::GetObjectAllocationBytes()
{
	return FEngineStatics::TotalAllocationBytes;
}

uint32 FStatOverlay::GetObjectAllocationCount()
{
	return FEngineStatics::TotalAllocationCount;
}

// UObject 추적치는 엔진이 직접 센 값이라 텍스처·버퍼 같은 다른 할당이 빠진다.
// 전체 사용량은 OS에 직접 묻는다.
uint64 FStatOverlay::GetProcessWorkingSetBytes()
{
	PROCESS_MEMORY_COUNTERS Counters{};
	if (!GetProcessMemoryInfo(GetCurrentProcess(), &Counters, sizeof(Counters)))
		return 0;
	return static_cast<uint64>(Counters.WorkingSetSize);
}
