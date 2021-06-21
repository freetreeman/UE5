// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/MirrorDataTable.h"
#include "AnimNode_Mirror.generated.h"


USTRUCT(BlueprintInternalUseOnly)
struct ANIMGRAPHRUNTIME_API FAnimNode_Mirror : public FAnimNode_Base
{
	GENERATED_BODY()

	friend class UAnimGraphNode_Mirror;

public:
	FAnimNode_Mirror();

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

	float GetBlendTimeOnMirrorStateChange() const { return BlendTimeOnMirrorStateChange; }

	UMirrorDataTable* GetMirrorDataTable() const;
	void SetMirrorDataTable(UMirrorDataTable* MirrorTable);

	UPROPERTY(EditAnywhere, Category = Links)
	FPoseLink Source;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinShownByDefault))
	bool bMirror;
private:

	UPROPERTY(EditAnywhere, Category = Settings)
	TObjectPtr<UMirrorDataTable> MirrorDataTable;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	float BlendTimeOnMirrorStateChange;

	UPROPERTY(EditAnywhere, Category = MirroredChannels, meta=(DisplayName="Bone"))
	bool bBoneMirroring;

	UPROPERTY(EditAnywhere, Category = MirroredChannels, meta=(DisplayName = "Curve"))
	bool bCurveMirroring;

	UPROPERTY(EditAnywhere, Category = MirroredChannels, meta=(DisplayName = "Attributes"))
	bool bAttributeMirroring;

	// Whether to reset (reinitialize) the child (source) pose when the mirror state changes
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bResetChildOnMirrorStateChange;

private:
	bool bMirrorState;
	bool bMirrorStateIsValid;

	void FillCompactPoseAndComponentRefRotations(const FBoneContainer& BoneContainer);
	// Compact pose format of Mirror Bone Map
	TArray<FCompactPoseBoneIndex> CompactPoseMirrorBones;

	// Pre-calculated component space of reference pose, which allows mirror to work with any joint orient 
	TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex> ComponentSpaceRefRotations;
};
