#pragma once

#include "Core/Stats/LightweightStats.h"

namespace RagdollStats
{
    DECLARE_CYCLE_STAT("Ragdoll: Kinematic Sync", STAT_KinematicSync);
    DECLARE_CYCLE_STAT("Ragdoll: Physics Pose", STAT_PhysicsPose);
}

struct FRagdollDebugStats
{
    int32 Instances = 0;
    int32 SimulatingInstances = 0;
    int32 Bodies = 0;
    int32 Joints = 0;
    double KinematicSyncMs = 0.0;
    double PhysicsPoseMs = 0.0;
};