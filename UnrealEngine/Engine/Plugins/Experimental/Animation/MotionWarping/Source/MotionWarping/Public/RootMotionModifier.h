// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSequence.h"
#include "RootMotionModifier.generated.h"

class UMotionWarpingComponent;
class UAnimNotifyState_MotionWarping;
class URootMotionModifier;

/** The possible states of a Root Motion Modifier */
UENUM(BlueprintType)
enum class ERootMotionModifierState : uint8
{
	/** The modifier is waiting for the animation to hit the warping window */
	Waiting,

	/** The modifier is active and currently affecting the final root motion */
	Active,

	/** The modifier has been marked for removal. Usually because the warping window is done */
	MarkedForRemoval,

	/** The modifier will remain in the list (as long as the window is active) but will not modify the root motion */
	Disabled
};

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnRootMotionModifierDelegate, UMotionWarpingComponent*, MotionWarpingComp, URootMotionModifier*, RootMotionModifier);

/** Represents a point of alignment in the world */
USTRUCT(BlueprintType, meta = (HasNativeMake = "MotionWarping.MotionWarpingUtilities.MakeMotionWarpingSyncPoint", HasNativeBreak = "MotionWarping.MotionWarpingUtilities.BreakMotionWarpingSyncPoint"))
struct MOTIONWARPING_API FMotionWarpingSyncPoint
{
	GENERATED_BODY()

	FMotionWarpingSyncPoint()
		: Location(FVector::ZeroVector), Rotation(FQuat::Identity) {}
	FMotionWarpingSyncPoint(const FVector& InLocation, const FQuat& InRotation)
		: Location(InLocation), Rotation(InRotation) {}
	FMotionWarpingSyncPoint(const FVector& InLocation, const FRotator& InRotation)
		: Location(InLocation), Rotation(InRotation.Quaternion()) {}
	FMotionWarpingSyncPoint(const FTransform& InTransform)
		: Location(InTransform.GetLocation()), Rotation(InTransform.GetRotation()) {}

	FORCEINLINE const FVector& GetLocation() const { return Location; }
	FORCEINLINE const FQuat& GetRotation() const { return Rotation; }
	FORCEINLINE FRotator Rotator() const { return Rotation.Rotator(); }

	FORCEINLINE bool operator==(const FMotionWarpingSyncPoint& Other) const
	{
		return Other.Location.Equals(Location) && Other.Rotation.Equals(Rotation);
	}

	FORCEINLINE bool operator!=(const FMotionWarpingSyncPoint& Other) const
	{
		return !Other.Location.Equals(Location) || !Other.Rotation.Equals(Rotation);
	}

protected:

	UPROPERTY()
	FVector Location;

	UPROPERTY()
	FQuat Rotation;
};

// FRootMotionModifier_Warp
///////////////////////////////////////////////////////////////

UENUM(BlueprintType)
enum class EMotionWarpRotationType : uint8
{
	/** Character rotates to match the rotation of the sync point */
	Default,

	/** Character rotates to face the sync point */
	Facing,
};

/** Method used to extract the warp point from the animation */
UENUM(BlueprintType)
enum class EWarpPointAnimProvider : uint8
{
	/** No warp point is provided */
	None,

	/** Warp point defined by a 'hard-coded' transform  user can enter through the warping notify */
	Static,

	/** Warp point defined by a bone */
	Bone
};

UCLASS(Abstract, BlueprintType, EditInlineNew)
class MOTIONWARPING_API URootMotionModifier : public UObject
{
	GENERATED_BODY()

public:

	/** Source of the root motion we are warping */
	UPROPERTY()
	TWeakObjectPtr<const UAnimSequenceBase> Animation = nullptr;

	/** Start time of the warping window */
	UPROPERTY()
	float StartTime = 0.f;

	/** End time of the warping window */
	UPROPERTY()
	float EndTime = 0.f;

	/** Previous playback time of the animation */
	UPROPERTY()
	float PreviousPosition = 0.f;

	/** Current playback time of the animation */
	UPROPERTY()
	float CurrentPosition = 0.f;

	/** Current blend weight of the animation */
	UPROPERTY()
	float Weight = 0.f;

	/** Whether this modifier runs before the extracted root motion is converted to world space or after */
	UPROPERTY()
	bool bInLocalSpace = false;

	/** Delegate called when this modifier is activated (starts affecting the root motion) */
	UPROPERTY()
	FOnRootMotionModifierDelegate OnActivateDelegate;

	/** Delegate called when this modifier updates while active (affecting the root motion) */
	UPROPERTY()
	FOnRootMotionModifierDelegate OnUpdateDelegate;

	/** Delegate called when this modifier is deactivated (stops affecting the root motion) */
	UPROPERTY()
	FOnRootMotionModifierDelegate OnDeactivateDelegate;

	URootMotionModifier(const FObjectInitializer& ObjectInitializer);

	/** Called when the state of the modifier changes */
	virtual void OnStateChanged(ERootMotionModifierState LastState);

	/** Sets the state of the modifier */
	void SetState(ERootMotionModifierState NewState);

	/** Returns the state of the modifier */
	FORCEINLINE ERootMotionModifierState GetState() const { return State; }

	/** Returns a pointer to the component that owns this modifier */
	UMotionWarpingComponent* GetOwnerComponent() const;

	/** Returns a pointer to the character that owns the component that owns this modifier */
	class ACharacter* GetCharacterOwner() const;

	virtual void Update();
	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) { return FTransform::Identity; }

	FORCEINLINE const UAnimSequenceBase* GetAnimation() const { return Animation.Get(); }

private:

	friend UMotionWarpingComponent;

	/** Current state */
	UPROPERTY()
	ERootMotionModifierState State = ERootMotionModifierState::Waiting;
};

UCLASS(meta = (DisplayName = "Simple Warp"))
class MOTIONWARPING_API URootMotionModifier_Warp : public URootMotionModifier
{
	GENERATED_BODY()

public:

	/** Name used to find the warp target for this modifier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (ExposeOnSpawn))
	FName WarpTargetName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	EWarpPointAnimProvider WarpPointAnimProvider = EWarpPointAnimProvider::None;

	//@TODO: Hide from the UI when Target != Static
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "WarpPointAnimProvider == EWarpPointAnimProvider::Static"))
	FTransform WarpPointAnimTransform = FTransform::Identity;

	//@TODO: Hide from the UI when Target != Bone
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "WarpPointAnimProvider == EWarpPointAnimProvider::Bone"))
	FName WarpPointAnimBoneName = NAME_None;

	/** Whether to warp the translation component of the root motion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	bool bWarpTranslation = true;

	/** Whether to ignore the Z component of the translation. Z motion will remain untouched */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "bWarpTranslation"))
	bool bIgnoreZAxis = true;

	/** Whether to warp the rotation component of the root motion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	bool bWarpRotation = true;

	/** Whether rotation should be warp to match the rotation of the sync point or to face the sync point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "bWarpRotation"))
	EMotionWarpRotationType RotationType;

	/**
	 * Allow to modify how fast the rotation is warped.
	 * e.g if the window duration is 2sec and this is 0.5, the target rotation will be reached in 1sec instead of 2sec
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config", meta = (EditCondition = "bWarpRotation"))
	float WarpRotationTimeMultiplier = 1.f;

	URootMotionModifier_Warp(const FObjectInitializer& ObjectInitializer);

	//~ Begin FRootMotionModifier Interface
	virtual void Update() override;
	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;
	//~ End FRootMotionModifier Interface

	/** Event called during update if the target transform changes while the warping is active */
	virtual void OnTargetTransformChanged() {}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void PrintLog(const FString& Name, const FTransform& OriginalRootMotion, const FTransform& WarpedRootMotion) const;
#endif

	UFUNCTION(BlueprintCallable, Category = "Motion Warping")
	static URootMotionModifier_Warp* AddRootMotionModifierSimpleWarp(UMotionWarpingComponent* InMotionWarpingComp, const UAnimSequenceBase* InAnimation, float InStartTime, float InEndTime,
		FName InWarpTargetName, EWarpPointAnimProvider InWarpPointAnimProvider, FTransform InWarpPointAnimTransform, FName InWarpPointAnimBoneName,
		bool bInWarpTranslation, bool bInIgnoreZAxis, bool bInWarpRotation, EMotionWarpRotationType InRotationType, float InWarpRotationTimeMultiplier = 1.f);

protected:

	FORCEINLINE FVector GetTargetLocation() const { return CachedTargetTransform.GetLocation(); }
	FORCEINLINE FRotator GetTargetRotator() const { return GetTargetRotation().Rotator(); }
	FQuat GetTargetRotation() const;

	FQuat WarpRotation(const FTransform& RootMotionDelta, const FTransform& RootMotionTotal, float DeltaSeconds);

	//@TODO: This should be Optional and private
	UPROPERTY()
	FTransform CachedTargetTransform = FTransform::Identity;

private:

	/** Cached of the offset from the warp target. Used to calculate the final target transform when a warp target is defined in the animation */
	TOptional<FTransform> CachedOffsetFromWarpPoint;
};

UCLASS(meta = (DisplayName = "Scale"))
class MOTIONWARPING_API URootMotionModifier_Scale : public URootMotionModifier
{
	GENERATED_BODY()

public:

	/** Vector used to scale each component of the translation */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	FVector Scale = FVector(1.f);

	URootMotionModifier_Scale(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) { bInLocalSpace = true; }

	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override
	{
		FTransform FinalRootMotion = InRootMotion;
		FinalRootMotion.ScaleTranslation(Scale);
		return FinalRootMotion;
	}
};