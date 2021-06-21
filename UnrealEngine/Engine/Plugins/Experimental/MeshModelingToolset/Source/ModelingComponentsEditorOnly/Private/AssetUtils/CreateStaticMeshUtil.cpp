// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetUtils/CreateStaticMeshUtil.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

#include "Engine/CollisionProfile.h"
#include "PhysicsEngine/BodySetup.h"

#include "MeshDescription.h"
#include "DynamicMeshToMeshDescription.h"

using namespace UE::AssetUtils;

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;



UE::AssetUtils::ECreateStaticMeshResult UE::AssetUtils::CreateStaticMeshAsset(
	FStaticMeshAssetOptions& Options,
	FStaticMeshResults& ResultsOut)
{
	FString NewObjectName = FPackageName::GetLongPackageAssetName(Options.NewAssetPath);

	UPackage* UsePackage = nullptr;
	if (Options.UsePackage != nullptr)
	{
		UsePackage = Options.UsePackage;
	}
	else
	{
		UsePackage = CreatePackage(*Options.NewAssetPath);
	}
	if (ensure(UsePackage != nullptr) == false)
	{
		return ECreateStaticMeshResult::InvalidPackage;
	}

	// create new UStaticMesh object
	EObjectFlags UseFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
	UStaticMesh* NewStaticMesh = NewObject<UStaticMesh>(UsePackage, FName(*NewObjectName), UseFlags);
	if (ensure(NewStaticMesh != nullptr) == false)
	{
		return ECreateStaticMeshResult::UnknownError;
	}

	// initialize the MeshDescription SourceModel LODs
	int32 UseNumSourceModels = FMath::Max(1, Options.NumSourceModels);
	NewStaticMesh->SetNumSourceModels(UseNumSourceModels);
	for (int32 k = 0; k < UseNumSourceModels; ++k)
	{
		FMeshBuildSettings& BuildSettings = NewStaticMesh->GetSourceModel(k).BuildSettings;

		BuildSettings.bRecomputeNormals = Options.bEnableRecomputeNormals;
		BuildSettings.bRecomputeTangents = Options.bEnableRecomputeTangents;
		BuildSettings.bGenerateLightmapUVs = Options.bGenerateLightmapUVs;

		if (!Options.bAllowDistanceField)
		{
			BuildSettings.DistanceFieldResolutionScale = 0.0f;
		}

		NewStaticMesh->CreateMeshDescription(k);
	}

	// create physics body and configure appropriately
	if (Options.bCreatePhysicsBody)
	{
		NewStaticMesh->CreateBodySetup();
		NewStaticMesh->GetBodySetup()->CollisionTraceFlag = Options.CollisionType;
	}

	// add a material slot. Must always have one material slot.
	int32 UseNumMaterialSlots = FMath::Max(1, Options.NumMaterialSlots);
	for (int MatIdx = 0; MatIdx < UseNumMaterialSlots; MatIdx++)
	{
		NewStaticMesh->GetStaticMaterials().Add(FStaticMaterial());
	}

	// set materials if the count matches
	if (Options.AssetMaterials.Num() == UseNumMaterialSlots)
	{
		for (int MatIdx = 0; MatIdx < UseNumMaterialSlots; MatIdx++)
		{
			NewStaticMesh->SetMaterial(MatIdx, Options.AssetMaterials[MatIdx]);
		}
	}

	// if options included SourceModel meshes, copy them over
	if (Options.SourceMeshes.MoveMeshDescriptions.Num() > 0)
	{
		if (ensure(Options.SourceMeshes.MoveMeshDescriptions.Num() == UseNumSourceModels))
		{
			for (int32 k = 0; k < UseNumSourceModels; ++k)
			{
				FMeshDescription* Mesh = NewStaticMesh->GetMeshDescription(k);
				*Mesh = MoveTemp(*Options.SourceMeshes.MoveMeshDescriptions[k]);
				NewStaticMesh->CommitMeshDescription(k);
			}
		}
	}
	else if (Options.SourceMeshes.MeshDescriptions.Num() > 0)
	{
		if (ensure(Options.SourceMeshes.MeshDescriptions.Num() == UseNumSourceModels))
		{
			for (int32 k = 0; k < UseNumSourceModels; ++k)
			{
				FMeshDescription* Mesh = NewStaticMesh->GetMeshDescription(k);
				*Mesh = *Options.SourceMeshes.MeshDescriptions[k];
				NewStaticMesh->CommitMeshDescription(k);
			}
		}
	}
	else if (Options.SourceMeshes.DynamicMeshes.Num() > 0)
	{
		if (ensure(Options.SourceMeshes.DynamicMeshes.Num() == UseNumSourceModels))
		{
			for (int32 k = 0; k < UseNumSourceModels; ++k)
			{
				FMeshDescription* Mesh = NewStaticMesh->GetMeshDescription(k);
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(Options.SourceMeshes.DynamicMeshes[k], *Mesh, !Options.bEnableRecomputeTangents);
				NewStaticMesh->CommitMeshDescription(k);
			}
		}
	}

	// Nanite options
	NewStaticMesh->NaniteSettings.bEnabled = Options.bGenerateNaniteEnabledMesh;
	if (Options.bGenerateNaniteEnabledMesh)
	{
		NewStaticMesh->NaniteSettings.PercentTriangles = Options.NaniteProxyTrianglePercent * 0.01f;
		NewStaticMesh->NaniteSettings.PositionPrecision = MIN_int32;
	}

	// Ray tracing
	NewStaticMesh->bSupportRayTracing = Options.bSupportRayTracing;

	// Distance field
	NewStaticMesh->bGenerateMeshDistanceField = Options.bAllowDistanceField;

	NewStaticMesh->MarkPackageDirty();
	if (Options.bDeferPostEditChange == false)
	{
		NewStaticMesh->PostEditChange();
	}

	ResultsOut.StaticMesh = NewStaticMesh;
	return ECreateStaticMeshResult::Ok;
}



