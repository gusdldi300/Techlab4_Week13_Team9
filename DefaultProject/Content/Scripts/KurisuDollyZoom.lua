-- Existing SunlitStation assets; only the camera moves.
-- HalfCycleSeconds controls one direction. A full loop lasts twice as long.
local HalfCycleSeconds = 6.0
local StartDistanceMeters = 6.0
local EndDistanceMeters = 1.5
local StartFocalLengthMm = 170.0
local CameraHeightMeters = 1.80
local ApertureFStop = 5.6
local FocusActorName = "SS_Focus_Kurisu_ExistingCharacter"

local ElapsedSeconds = 0.0
local Paused = false
local DepthOfFieldEnabled = true
local FocusActor = nil

local function ApplyCamera()
    if not FocusActor or not FocusActor:IsValid() then
        FocusActor = GetActorByName(FocusActorName)
    end
    if not FocusActor then return end

    -- Cosine motion turns around smoothly without a cut at either endpoint.
    local Phase = ElapsedSeconds * math.pi / HalfCycleSeconds
    local Alpha = (1.0 - math.cos(Phase)) * 0.5
    local Distance = StartDistanceMeters
        + (EndDistanceMeters - StartDistanceMeters) * Alpha
    local FocalLength = StartFocalLengthMm * Distance / StartDistanceMeters
    local TargetLocation = FocusActor.Location

    -- Constant focal-length / target-depth ratio preserves the upper-body size.
    obj.Location = FVector(
        TargetLocation.x - Distance,
        TargetLocation.y,
        CameraHeightMeters)
    obj.Rotation = FRotator(0.0, 0.0, 0.0)
    -- Camera autofocus uses the animated head depth when available.
    SetCameraOptics(obj, FocalLength, Distance, ApertureFStop, DepthOfFieldEnabled)
end

function BeginPlay()
    assert(type(SetCameraOptics) == "function",
        "[DollyZoom] SetCameraOptics is unavailable. Rebuild and restart the editor.")
    FocusActor = GetActorByName(FocusActorName)
    assert(FocusActor and FocusActor:IsValid(),
        "[DollyZoom] Missing focus actor: " .. FocusActorName)
    ElapsedSeconds = 0.0
    Paused = false
    DepthOfFieldEnabled = true
    ApplyCamera()
    assert(SetViewTargetWithBlend(obj, 0.0),
        "[DollyZoom] Failed to activate the camera view target.")
    print(string.format(
        "[DollyZoom] %.0fs each way / %.0fs loop. Space: pause, R: restart, C: DOF.",
        HalfCycleSeconds, HalfCycleSeconds * 2.0))
end

function Tick(DeltaSeconds)
    if IsKeyPressed("Space") then Paused = not Paused end
    if IsKeyPressed("R") then ElapsedSeconds = 0.0 end
    if IsKeyPressed("C") then DepthOfFieldEnabled = not DepthOfFieldEnabled end
    if not Paused then
        ElapsedSeconds = (ElapsedSeconds + math.max(DeltaSeconds, 0.0))
            % (HalfCycleSeconds * 2.0)
    end
    ApplyCamera()
end
