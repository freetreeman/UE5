// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrameTypes.h"
#include "VectorTypes.h"

class AVolume;
PREDECLARE_GEOMETRY(class FDynamicMesh3);

namespace UE {
namespace Conversion {

struct FVolumeToMeshOptions
{
	bool bInWorldSpace = false;
	bool bSetGroups = true;
	bool bMergeVertices = true;
	bool bAutoRepairMesh = true;
	bool bOptimizeMesh = true;
};

/**
 * Converts a volume to a dynamic mesh. Does not initialize normals and does not delete the volume.
 */
void MODELINGCOMPONENTS_API VolumeToDynamicMesh(AVolume* Volume, UE::Geometry::FDynamicMesh3& Mesh, const FVolumeToMeshOptions& Options);

}}//end namespace UE::Conversion