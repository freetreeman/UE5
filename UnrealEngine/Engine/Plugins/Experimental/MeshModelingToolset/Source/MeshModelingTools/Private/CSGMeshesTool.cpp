// Copyright Epic Games, Inc. All Rights Reserved.

#include "CSGMeshesTool.h"
#include "CompositionOps/BooleanMeshesOp.h"

#include "ToolSetupUtil.h"
#include "BaseGizmos/TransformProxy.h"

#include "DynamicMesh/DynamicMesh3.h"

#include "MeshDescriptionToDynamicMesh.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UCSGMeshesTool"

void UCSGMeshesTool::EnableTrimMode()
{
	check(OriginalDynamicMeshes.Num() == 0);		// must not have been initialized!
	bTrimMode = true;
}

void UCSGMeshesTool::SetupProperties()
{
	Super::SetupProperties();

	if (bTrimMode)
	{
		TrimProperties = NewObject<UTrimMeshesToolProperties>(this);
		TrimProperties->RestoreProperties(this);
		AddToolPropertySource(TrimProperties);

		TrimProperties->WatchProperty(TrimProperties->WhichMesh, [this](ETrimOperation)
		{
			UpdateGizmoVisibility();
			UpdatePreviewsVisibility();
		});
		TrimProperties->WatchProperty(TrimProperties->bShowTrimmingMesh, [this](bool)
		{
			UpdatePreviewsVisibility();
		});
		TrimProperties->WatchProperty(TrimProperties->ColorOfTrimmingMesh, [this](FLinearColor)
		{
			UpdatePreviewsMaterial();
		});
		TrimProperties->WatchProperty(TrimProperties->OpacityOfTrimmingMesh, [this](float)
		{
			UpdatePreviewsMaterial();
		});

		SetToolDisplayName(LOCTEXT("TrimMeshesToolName", "Trim"));
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartTrimTool", "Trim one mesh with another. Use the transform gizmos to tweak the positions of the input objects (can help to resolve errors/failures)"),
			EToolMessageLevel::UserNotification);
	}
	else
	{
		CSGProperties = NewObject<UCSGMeshesToolProperties>(this);
		CSGProperties->RestoreProperties(this);
		AddToolPropertySource(CSGProperties);

		CSGProperties->WatchProperty(CSGProperties->Operation, [this](ECSGOperation)
		{
			UpdateGizmoVisibility();
			UpdatePreviewsVisibility();
		});
		CSGProperties->WatchProperty(CSGProperties->bShowSubtractedMesh, [this](bool)
		{
			UpdatePreviewsVisibility();
		});
		CSGProperties->WatchProperty(CSGProperties->ColorOfSubtractedMesh, [this](FLinearColor)
		{
			UpdatePreviewsMaterial();
		});
		CSGProperties->WatchProperty(CSGProperties->OpacityOfSubtractedMesh, [this](float)
		{
			UpdatePreviewsMaterial();
		});

		SetToolDisplayName(LOCTEXT("CSGMeshesToolName", "Boolean"));
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartTool", "Compute CSG Booleans on the input meshes. Use the transform gizmos to tweak the positions of the input objects (can help to resolve errors/failures)"),
			EToolMessageLevel::UserNotification);
	}
}

void UCSGMeshesTool::UpdatePreviewsMaterial()
{
	if (!PreviewsGhostMaterial)
	{
		PreviewsGhostMaterial = ToolSetupUtil::GetSimpleCustomMaterial(GetToolManager(), FLinearColor::Black, .2);
	}
	FLinearColor Color;
	float Opacity;
	if (bTrimMode)
	{
		Color = TrimProperties->ColorOfTrimmingMesh;
		Opacity = TrimProperties->OpacityOfTrimmingMesh;
	}
	else
	{
		Color = CSGProperties->ColorOfSubtractedMesh;
		Opacity = CSGProperties->OpacityOfSubtractedMesh;
	}
	PreviewsGhostMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	PreviewsGhostMaterial->SetScalarParameterValue(TEXT("Opacity"), Opacity);
}

void UCSGMeshesTool::UpdatePreviewsVisibility()
{
	int32 ShowPreviewIdx = -1;
	if (bTrimMode && TrimProperties->bShowTrimmingMesh)
	{
		ShowPreviewIdx = TrimProperties->WhichMesh == ETrimOperation::TrimA ? OriginalMeshPreviews.Num() - 1 : 0;
	}
	else if (!bTrimMode && CSGProperties->bShowSubtractedMesh)
	{
		if (CSGProperties->Operation == ECSGOperation::DifferenceAB)
		{
			ShowPreviewIdx = OriginalMeshPreviews.Num() - 1;
		}
		else if (CSGProperties->Operation == ECSGOperation::DifferenceBA)
		{
			ShowPreviewIdx = 0;
		}
	}
	for (int32 MeshIdx = 0; MeshIdx < OriginalMeshPreviews.Num(); MeshIdx++)
	{
		OriginalMeshPreviews[MeshIdx]->SetVisible(ShowPreviewIdx == MeshIdx);
	}
}

int32 UCSGMeshesTool::GetHiddenGizmoIndex() const
{
	int32 ParentHiddenIndex = Super::GetHiddenGizmoIndex();
	if (ParentHiddenIndex != -1)
	{
		return ParentHiddenIndex;
	}
	if (bTrimMode)
	{
		return TrimProperties->WhichMesh == ETrimOperation::TrimA ? 0 : 1;
	}
	else if (CSGProperties->Operation == ECSGOperation::DifferenceAB)
	{
		return 0;
	}
	else if (CSGProperties->Operation == ECSGOperation::DifferenceBA)
	{
		return 1;
	}
	else
	{
		return -1;
	}
}

void UCSGMeshesTool::SaveProperties()
{
	Super::SaveProperties();
	if (bTrimMode)
	{
		TrimProperties->SaveProperties(this);
	}
	else
	{
		CSGProperties->SaveProperties(this);
	}
}


void UCSGMeshesTool::ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh)
{
	OriginalDynamicMeshes.SetNum(Targets.Num());
	FComponentMaterialSet AllMaterialSet;
	TMap<UMaterialInterface*, int> KnownMaterials;
	TArray<TArray<int>> MaterialRemap; MaterialRemap.SetNum(Targets.Num());

	if (bTrimMode || !CSGProperties->bOnlyUseFirstMeshMaterials)
	{
		for (int ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
			FComponentMaterialSet ComponentMaterialSet;
			TargetMaterialInterface(ComponentIdx)->GetMaterialSet(ComponentMaterialSet);
			for (UMaterialInterface* Mat : ComponentMaterialSet.Materials)
			{
				int* FoundMatIdx = KnownMaterials.Find(Mat);
				int MatIdx;
				if (FoundMatIdx)
				{
					MatIdx = *FoundMatIdx;
				}
				else
				{
					MatIdx = AllMaterialSet.Materials.Add(Mat);
					KnownMaterials.Add(Mat, MatIdx);
				}
				MaterialRemap[ComponentIdx].Add(MatIdx);
			}
		}
	}
	else
	{
		TargetMaterialInterface(0)->GetMaterialSet(AllMaterialSet);
		for (int MatIdx = 0; MatIdx < AllMaterialSet.Materials.Num(); MatIdx++)
		{
			MaterialRemap[0].Add(MatIdx);
		}
		for (int ComponentIdx = 1; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
			MaterialRemap[ComponentIdx].Init(0, TargetMaterialInterface(ComponentIdx)->GetNumMaterials());
		}
	}

	UpdatePreviewsMaterial();
	for (int ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		OriginalDynamicMeshes[ComponentIdx] = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(TargetMeshProviderInterface(ComponentIdx)->GetMeshDescription(), *OriginalDynamicMeshes[ComponentIdx]);

		// ensure materials and attributes are always enabled
		OriginalDynamicMeshes[ComponentIdx]->EnableAttributes();
		OriginalDynamicMeshes[ComponentIdx]->Attributes()->EnableMaterialID();
		FDynamicMeshMaterialAttribute* MaterialIDs = OriginalDynamicMeshes[ComponentIdx]->Attributes()->GetMaterialID();
		for (int TID : OriginalDynamicMeshes[ComponentIdx]->TriangleIndicesItr())
		{
			MaterialIDs->SetValue(TID, MaterialRemap[ComponentIdx][MaterialIDs->GetValue(TID)]);
		}

		if (bSetPreviewMesh)
		{
			UPreviewMesh* OriginalMeshPreview = OriginalMeshPreviews.Add_GetRef(NewObject<UPreviewMesh>());
			OriginalMeshPreview->CreateInWorld(TargetWorld, TargetComponentInterface(ComponentIdx)->GetWorldTransform());
			OriginalMeshPreview->UpdatePreview(OriginalDynamicMeshes[ComponentIdx].Get());

			OriginalMeshPreview->SetMaterial(0, PreviewsGhostMaterial);
			OriginalMeshPreview->SetVisible(false);
			TransformProxies[ComponentIdx]->AddComponent(OriginalMeshPreview->GetRootComponent());
		}
	}
	Preview->ConfigureMaterials(AllMaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
}


void UCSGMeshesTool::SetPreviewCallbacks()
{	
	DrawnLineSet = NewObject<ULineSetComponent>(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetupAttachment(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	DrawnLineSet->RegisterComponent();

	Preview->OnOpCompleted.AddLambda(
		[this](const FDynamicMeshOperator* Op)
		{
			const FBooleanMeshesOp* BooleanOp = (const FBooleanMeshesOp*)(Op);
			CreatedBoundaryEdges = BooleanOp->GetCreatedBoundaryEdges();
		}
	);
	Preview->OnMeshUpdated.AddLambda(
		[this](const UMeshOpPreviewWithBackgroundCompute*)
		{
			GetToolManager()->PostInvalidation();
			UpdateVisualization();
		}
	);
}


void UCSGMeshesTool::UpdateVisualization()
{
	FColor BoundaryEdgeColor(240, 15, 15);
	float BoundaryEdgeThickness = 2.0;
	float BoundaryEdgeDepthBias = 2.0f;

	const FDynamicMesh3* TargetMesh = Preview->PreviewMesh->GetPreviewDynamicMesh();
	FVector3d A, B;

	DrawnLineSet->Clear();
	if (!bTrimMode && CSGProperties->bShowNewBoundaryEdges)
	{
		for (int EID : CreatedBoundaryEdges)
		{
			TargetMesh->GetEdgeV(EID, A, B);
			DrawnLineSet->AddLine((FVector)A, (FVector)B, BoundaryEdgeColor, BoundaryEdgeThickness, BoundaryEdgeDepthBias);
		}
	}
}


TUniquePtr<FDynamicMeshOperator> UCSGMeshesTool::MakeNewOperator()
{
	TUniquePtr<FBooleanMeshesOp> BooleanOp = MakeUnique<FBooleanMeshesOp>();
	
	BooleanOp->bTrimMode = bTrimMode;
	if (bTrimMode)
	{
		BooleanOp->TrimOperation = TrimProperties->WhichMesh;
		BooleanOp->TrimSide = TrimProperties->TrimSide;
		BooleanOp->bAttemptFixHoles = false;
		BooleanOp->bTryCollapseExtraEdges = false;
	}
	else
	{
		BooleanOp->CSGOperation = CSGProperties->Operation;
		BooleanOp->bAttemptFixHoles = CSGProperties->bAttemptFixHoles;
		BooleanOp->bTryCollapseExtraEdges = CSGProperties->bCollapseExtraEdges;
	}

	check(OriginalDynamicMeshes.Num() == 2);
	check(Targets.Num() == 2);
	BooleanOp->Transforms.SetNum(2);
	BooleanOp->Meshes.SetNum(2);
	for (int Idx = 0; Idx < 2; Idx++)
	{
		BooleanOp->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		BooleanOp->Transforms[Idx] = TransformProxies[Idx]->GetTransform();
	}

	return BooleanOp;
}



void UCSGMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCSGMeshesToolProperties, bOnlyUseFirstMeshMaterials)))
	{
		if (!AreAllTargetsValid())
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTargets", "Target meshes are no longer valid"), EToolMessageLevel::UserWarning);
			return;
		}
		ConvertInputsAndSetPreviewMaterials(false);
		Preview->InvalidateResult();
	}
	else if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCSGMeshesToolProperties, bShowNewBoundaryEdges)))
	{
		GetToolManager()->PostInvalidation();
		UpdateVisualization();
	}
	else
	{
		Super::OnPropertyModified(PropertySet, Property);
	}
}


FString UCSGMeshesTool::GetCreatedAssetName() const
{
	if (bTrimMode)
	{
		return TEXT("Trim");
	}
	else
	{
		return TEXT("Boolean");
	}
}


FText UCSGMeshesTool::GetActionName() const
{
	if (bTrimMode)
	{
		return LOCTEXT("CSGMeshes", "Trim Meshes");
	}
	else
	{
		return LOCTEXT("CSGMeshes", "Boolean Meshes");
	}
}



void UCSGMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	Super::Shutdown(ShutdownType);

	for (UPreviewMesh* MeshPreview : OriginalMeshPreviews)
	{
		MeshPreview->SetVisible(false);
		MeshPreview->Disconnect();
		MeshPreview = nullptr;
	}
}



#undef LOCTEXT_NAMESPACE
