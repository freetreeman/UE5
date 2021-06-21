// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearch.h"
#include "PoseSearch/PoseSearchPredictionTypes.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNode_MotionMatching.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct POSESEARCH_API FAnimNode_MotionMatching : public FAnimNode_AssetPlayerBase
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPoseLink Source;

	// Collection of animations for motion matching
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	TObjectPtr<const UPoseSearchDatabase> Database = nullptr;

	// Dynamic biasing weights during pose search queries
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	FPoseSearchBiasWeightParams BiasWeights;

	// Motion matching goal
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	FPoseSearchFeatureVectorBuilder Goal;

	// Trajectory prediction samples
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	FPredictionTrajectoryRange Prediction;

	// Configuration for prediction play rate adjustment, error tolerances, and sampling resolution
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (PinHiddenByDefault))
	FPredictionTrajectorySettings PredictionSettings;

	// Time in seconds to blend out to the new pose. Uses inertial blending and requires an Inertialization node after this node.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float BlendTime = 0.2f;

	// Don't jump to poses that are less than this many seconds away
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float PoseJumpThreshold = 4.0f;

	// Minimum amount of time to wait between pose searches
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float SearchThrottleTime = 0.1f;

	// How much better the search result must be compared to the current pose in order to jump to it
	// Note: This feature won't work quite as advertised until search data rescaling is implemented
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(ClampMin="0"))
	float MinPercentImprovement = 0.0f;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=Debug)
	bool bDebugDraw = false;

	UPROPERTY(EditAnywhere, Category=Debug)
	bool bDebugDrawGoal = true;

	UPROPERTY(EditAnywhere, Category=Debug)
	bool bDebugDrawMatch = true;
#endif

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual bool HasPreUpdate() const override;
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	// End of FAnimNode_Base interface

private:

	// Embedded sequence player node for playing animations from the motion matching database
	FAnimNode_SequencePlayer_Standalone SequencePlayerNode;

	// The current pose we're playing from the database
	int32 DbPoseIdx = INDEX_NONE;

	// The current animation we're playing from the database
	int32 DbSequenceIdx = INDEX_NONE;

	// The current query feature vector used to search the database for pose candidates
	FPoseSearchFeatureVectorBuilder ComposedQuery;

	// When the current database is different from the previous one, the search parameters are reset
	UPROPERTY(Transient)
	TObjectPtr<const UPoseSearchDatabase> PreviousDatabase = nullptr;

	// Time since the last pose jump
	float ElapsedPoseJumpTime = 0.0f;

	bool IsValidForSearch() const;
	void ComposeQuery(const FAnimationBaseContext& Context);
	void JumpToPose(const FAnimationUpdateContext& Context, UE::PoseSearch::FDbSearchResult Result);
	void InitNewDatabaseSearch();
	
	// FAnimNode_AssetPlayerBase
protected:
	// FAnimNode_AssetPlayerBase interface
	virtual float GetAccumulatedTime() const;
	virtual UAnimationAsset* GetAnimAsset() const override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual float GetCurrentAssetLength() const override;
	virtual float GetCurrentAssetTime() const override;
	virtual float GetCurrentAssetTimePlayRateAdjusted() const override;
	virtual bool GetIgnoreForRelevancyTest() const override;
	virtual bool SetIgnoreForRelevancyTest(bool bInIgnoreForRelevancyTest) override;
	// End of FAnimNode_AssetPlayerBase interface

private:

#if WITH_EDITORONLY_DATA
	// If true, "Relevant anim" nodes that look for the highest weighted animation in a state will ignore this node
	UPROPERTY(EditAnywhere, Category=Relevancy, meta=(FoldProperty, PinHiddenByDefault))
	bool bIgnoreForRelevancyTest = false;
#endif // WITH_EDITORONLY_DATA
};