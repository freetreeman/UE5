// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseOps/SimpleMeshProcessingBaseOp.h"
#include "DynamicMeshEditor.h"
#include "DynamicMesh/MeshNormals.h"
#include "MeshWeights.h"
#include "MeshBoundaryLoops.h"
#include "Operations/JoinMeshLoops.h"
#include "Solvers/ConstrainedMeshDeformer.h"
#include "Implicit/GridInterpolant.h"
#include "Operations/FFDLattice.h"

namespace UE
{
namespace Geometry
{

class MODELINGOPERATORS_API FLatticeDeformerOp : public FDynamicMeshOperator
{
public:

	FLatticeDeformerOp(TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> InOriginalMesh,
					   TSharedPtr<UE::Geometry::FFFDLattice, ESPMode::ThreadSafe> InLattice,
					   const TArray<FVector3d>& InLatticeControlPoints,
					   UE::Geometry::ELatticeInterpolation InInterpolationType,
					   bool bInDeformNormals);

	// FDynamicMeshOperator implementation
	void CalculateResult(FProgressCancel* Progress) override;

protected:

	// Inputs
	const TSharedPtr<const UE::Geometry::FFFDLattice, ESPMode::ThreadSafe> Lattice;
	const TSharedPtr<const FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	const TArray<FVector3d> LatticeControlPoints;
	UE::Geometry::ELatticeInterpolation InterpolationType;
	bool bDeformNormals = false;
};

} // end namespace UE::Geometry
} // end namespace UE

