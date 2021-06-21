// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Spatial/MeshAABBTree3.h"
#include "MeshAdapter.h"


/**
 * Basic struct to adapt a FMeshDescription for use by GeometryProcessing classes that template the mesh type and expect a standard set of basic accessors
 * For example, this adapter will let you use a FMeshDescription with GeometryProcessing's TMeshAABBTree3
 * See also the Editable version below
 *
 *  Usage example -- given some const FMeshDescription* Mesh:
 *    FMeshDescriptionAABBAdapter MeshAdapter(Mesh); // adapt the mesh
 *    TMeshAABBTree3<const FMeshDescriptionTriangleMeshAdapter> AABBTree(&MeshAdapter); // provide the adapter to a templated class like TMeshAABBTree3
 */
struct /*MESHCONVERSION_API*/ FMeshDescriptionTriangleMeshAdapter
{
	using FIndex3i = UE::Geometry::FIndex3i;
protected:
	const FMeshDescription* Mesh;
	TVertexAttributesConstRef<FVector3f> VertexPositions;
	TVertexInstanceAttributesConstRef<FVector3f> VertexInstanceNormals;

	FVector3d BuildScale = FVector3d::One();
	bool bScaleNormals = false;

public:
	FMeshDescriptionTriangleMeshAdapter(const FMeshDescription* MeshIn) : Mesh(MeshIn)
	{
		FStaticMeshConstAttributes Attributes(*MeshIn);
		VertexPositions = Attributes.GetVertexPositions();
		VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
		// @todo: can we hold TArrayViews of the attribute arrays here? Do we guarantee not to mutate the mesh description for the duration of this object?
	}

	void SetBuildScale(const FVector3d& BuildScaleIn, bool bScaleNormalsIn)
	{
		BuildScale = BuildScaleIn;
		bScaleNormals = bScaleNormalsIn;
	}

	bool IsTriangle(int32 TID) const
	{
		return TID >= 0 && TID < Mesh->Triangles().Num();
	}
	bool IsVertex(int32 VID) const
	{
		return VID >= 0 && VID < Mesh->Vertices().Num();
	}
	// ID and Count are the same for MeshDescription because it's compact
	int32 MaxTriangleID() const
	{
		return Mesh->Triangles().Num();
	}
	int32 TriangleCount() const
	{
		return Mesh->Triangles().Num();
	}
	int32 MaxVertexID() const
	{
		return Mesh->Vertices().Num();
	}
	int32 VertexCount() const
	{
		return Mesh->Vertices().Num();
	}
	int32 GetShapeTimestamp() const
	{
		// MeshDescription doesn't provide any mechanism to know if it's been modified so just return 0
		// and leave it to the caller to not build an aabb and then change the underlying mesh
		return 0;
	}
	FIndex3i GetTriangle(int32 IDValue) const
	{
		TArrayView<const FVertexID> TriVertIDs = Mesh->GetTriangleVertices(FTriangleID(IDValue));
		return FIndex3i(TriVertIDs[0].GetValue(), TriVertIDs[1].GetValue(), TriVertIDs[2].GetValue());
	}
	FVector3d GetVertex(int32 IDValue) const
	{
		const FVector& Position = VertexPositions[FVertexID(IDValue)];
		return FVector3d(BuildScale.X * (double)Position.X, BuildScale.Y * (double)Position.Y, BuildScale.Z * (double)Position.Z);
	}

	inline void GetTriVertices(int32 IDValue, FVector3d& V0, FVector3d& V1, FVector3d& V2) const
	{
		TArrayView<const FVertexID> TriVertIDs = Mesh->GetTriangleVertices(FTriangleID(IDValue));
		const FVector& A = VertexPositions[TriVertIDs[0]];
		V0 = FVector3d(BuildScale.X * (double)A.X, BuildScale.Y * (double)A.Y, BuildScale.Z * (double)A.Z);
		const FVector& B = VertexPositions[TriVertIDs[1]];
		V1 = FVector3d(BuildScale.X * (double)B.X, BuildScale.Y * (double)B.Y, BuildScale.Z * (double)B.Z);
		const FVector& C = VertexPositions[TriVertIDs[2]];
		V2 = FVector3d(BuildScale.X * (double)C.X, BuildScale.Y * (double)C.Y, BuildScale.Z * (double)C.Z);
	}

	template<typename VectorType>
	inline void GetTriVertices(int32 IDValue, VectorType& V0, VectorType& V1, VectorType& V2) const
	{
		TArrayView<const FVertexID> TriVertIDs = Mesh->GetTriangleVertices(FTriangleID(IDValue));
		const FVector& A = VertexPositions[TriVertIDs[0]];
		V0 = VectorType(BuildScale.X * (double)A.X, BuildScale.Y * (double)A.Y, BuildScale.Z * (double)A.Z);
		const FVector& B = VertexPositions[TriVertIDs[1]];
		V1 = VectorType(BuildScale.X * (double)B.X, BuildScale.Y * (double)B.Y, BuildScale.Z * (double)B.Z);
		const FVector& C = VertexPositions[TriVertIDs[2]];
		V2 = VectorType(BuildScale.X * (double)C.X, BuildScale.Y * (double)C.Y, BuildScale.Z * (double)C.Z);
	}

	inline bool HasNormals() const
	{
		return VertexInstanceNormals.IsValid();
	}
	inline bool IsNormal(int32 NID) const
	{
		return HasNormals() && NID >= 0 && NID < NormalCount();
	}
	inline int32 MaxNormalID() const
	{
		return HasNormals() ? VertexInstanceNormals.GetNumElements() : 0;
	}
	inline int32 NormalCount() const
	{
		return HasNormals() ? VertexInstanceNormals.GetNumElements() : 0;
	}
	FVector3f GetNormal(int32 IDValue) const
	{
		const FVector& InstanceNormal = VertexInstanceNormals[FVertexInstanceID(IDValue)];
		return (!bScaleNormals) ? FVector3f(InstanceNormal) :
			UE::Geometry::Normalized(FVector3f(InstanceNormal.X/BuildScale.X, InstanceNormal.Y/BuildScale.Y, InstanceNormal.Z/BuildScale.Z));
	}
};


/**
 * Non-const version of the adapter, with non-const storage and setters
 * TODO: try to be smarter about sharing code w/ the above const version
 */
struct /*MESHCONVERSION_API*/ FMeshDescriptionEditableTriangleMeshAdapter
{
	using FIndex3i = UE::Geometry::FIndex3i;
protected:
	FMeshDescription* Mesh;
	TVertexAttributesRef<FVector3f> VertexPositions;
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals;

public:
	FMeshDescriptionEditableTriangleMeshAdapter(FMeshDescription* MeshIn) : Mesh(MeshIn)
	{
		FStaticMeshAttributes Attributes(*MeshIn);
		VertexPositions = Attributes.GetVertexPositions();
		VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
	}

	bool IsTriangle(int32 TID) const
	{
		return TID >= 0 && TID < Mesh->Triangles().Num();
	}
	bool IsVertex(int32 VID) const
	{
		return VID >= 0 && VID < Mesh->Vertices().Num();
	}
	// ID and Count are the same for MeshDescription because it's compact
	int32 MaxTriangleID() const
	{
		return Mesh->Triangles().Num();
	}
	int32 TriangleCount() const
	{
		return Mesh->Triangles().Num();
	}
	int32 MaxVertexID() const
	{
		return Mesh->Vertices().Num();
	}
	int32 VertexCount() const
	{
		return Mesh->Vertices().Num();
	}
	int32 GetShapeTimestamp() const
	{
		// MeshDescription doesn't provide any mechanism to know if it's been modified so just return 0
		// and leave it to the caller to not build an aabb and then change the underlying mesh
		return 0;
	}
	FIndex3i GetTriangle(int32 IDValue) const
	{
		TArrayView<const FVertexID> TriVertIDs = Mesh->GetTriangleVertices(FTriangleID(IDValue));
		return FIndex3i(TriVertIDs[0].GetValue(), TriVertIDs[1].GetValue(), TriVertIDs[2].GetValue());
	}
	FVector3d GetVertex(int32 IDValue) const
	{
		return FVector3d(VertexPositions[FVertexID(IDValue)]);
	}
	void SetVertex(int32 IDValue, const FVector3d& NewPos)
	{
		VertexPositions[FVertexID(IDValue)] = (FVector)NewPos;
	}

	inline void GetTriVertices(int32 IDValue, FVector3d& V0, FVector3d& V1, FVector3d& V2) const
	{
		TArrayView<const FVertexID> TriVertIDs = Mesh->GetTriangleVertices(FTriangleID(IDValue));
		V0 = FVector3d(VertexPositions[TriVertIDs[0]]);
		V1 = FVector3d(VertexPositions[TriVertIDs[1]]);
		V2 = FVector3d(VertexPositions[TriVertIDs[2]]);
	}


	inline bool HasNormals() const
	{
		return VertexInstanceNormals.IsValid();
	}
	inline bool IsNormal(int32 NID) const
	{
		return HasNormals() && NID >= 0 && NID < NormalCount();
	}
	inline int32 MaxNormalID() const
	{
		return HasNormals() ? VertexInstanceNormals.GetNumElements() : 0;
	}
	inline int32 NormalCount() const
	{
		return HasNormals() ? VertexInstanceNormals.GetNumElements() : 0;
	}
	FVector3f GetNormal(int32 IDValue) const
	{
		return FVector3f(VertexInstanceNormals[FVertexInstanceID(IDValue)]);
	}
	void SetNormal(int32 IDValue, const FVector3f& Normal)
	{
		VertexInstanceNormals[FVertexInstanceID(IDValue)] = (FVector)Normal;
	}
};



/**
 * TTriangleMeshAdapter version of FMeshDescriptionTriangleMeshAdapter
 */
struct FMeshDescriptionMeshAdapterd : public UE::Geometry::TTriangleMeshAdapter<double>
{
	FMeshDescriptionTriangleMeshAdapter ParentAdapter;

	FMeshDescriptionMeshAdapterd(const FMeshDescription* MeshIn) : ParentAdapter(MeshIn)
	{
		IsTriangle = [&](int index) { return ParentAdapter.IsTriangle(index);};
		IsVertex = [&](int index) { return ParentAdapter.IsVertex(index); };
		MaxTriangleID = [&]() { return ParentAdapter.MaxTriangleID();};
		MaxVertexID = [&]() { return ParentAdapter.MaxVertexID();};
		TriangleCount = [&]() { return ParentAdapter.TriangleCount();};
		VertexCount = [&]() { return ParentAdapter.VertexCount();};
		GetShapeTimestamp = [&]() { return ParentAdapter.GetShapeTimestamp();};
		GetTriangle = [&](int32 TriangleID) { return ParentAdapter.GetTriangle(TriangleID); };
		GetVertex = [&](int32 VertexID) { return ParentAdapter.GetVertex(VertexID); };
	}

	FMeshDescriptionMeshAdapterd(FMeshDescriptionTriangleMeshAdapter ParentAdapterIn) : ParentAdapter(ParentAdapterIn)
	{
		IsTriangle = [&](int index) { return ParentAdapter.IsTriangle(index);};
		IsVertex = [&](int index) { return ParentAdapter.IsVertex(index); };
		MaxTriangleID = [&]() { return ParentAdapter.MaxTriangleID();};
		MaxVertexID = [&]() { return ParentAdapter.MaxVertexID();};
		TriangleCount = [&]() { return ParentAdapter.TriangleCount();};
		VertexCount = [&]() { return ParentAdapter.VertexCount();};
		GetShapeTimestamp = [&]() { return ParentAdapter.GetShapeTimestamp();};
		GetTriangle = [&](int32 TriangleID) { return ParentAdapter.GetTriangle(TriangleID); };
		GetVertex = [&](int32 VertexID) { return ParentAdapter.GetVertex(VertexID); };
	}

};
