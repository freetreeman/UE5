// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/WorldPartitionActorDescView.h"
#include "ProfilingDebugging/ProfilingHelpers.h"
#include "Algo/AnyOf.h"
#include "WorldPartitionRuntimeCell.generated.h"

USTRUCT()
struct FWorldPartitionRuntimeCellObjectMapping
{
	GENERATED_USTRUCT_BODY()

	FWorldPartitionRuntimeCellObjectMapping()
#if WITH_EDITORONLY_DATA
		: Package(NAME_None)
		, Path(NAME_None)
		, ContainerID(0)
		, ContainerTransform(FTransform::Identity)
		, ContainerPackage(NAME_None)
		, LoadedPath(NAME_None)
#endif
	{}

	FWorldPartitionRuntimeCellObjectMapping(FName InPackage, FName InPath, uint64 InContainerID, const FTransform& InContainerTransform, FName InContainerPackage)
#if WITH_EDITORONLY_DATA
		: Package(InPackage)
		, Path(InPath)
		, ContainerID(InContainerID)
		, ContainerTransform(InContainerTransform)
		, ContainerPackage(InContainerPackage)
		, LoadedPath(InPath)
#endif
	{}

#if WITH_EDITORONLY_DATA
	/** 
	 * The name of the package to load to resolve on disk (can contain a single actor or a data chunk)
	 */
	UPROPERTY()
	FName Package;

	/** 
	 * The complete name path of the contained object
	 */
	UPROPERTY()
	FName Path;

	/**
	 * ID of the owning container instance
	 */
	UPROPERTY()
	uint64 ContainerID;

	/** 
	 * Transform of the owning container instance
	 */
	UPROPERTY()
	FTransform ContainerTransform;
		
	/**
	 * Package of the owning container instance
	 */
	UPROPERTY()
	FName ContainerPackage;

	/**
	* Loaded actor path (when cooking or pie)
	* 
	* Depending on if the actor was part of a container instance or the main partition this will be the path
	* of the loaded or duplicated actor before it is moved into its runtime cell.
	* 
	* If the actor was part of the world partition this path should match the Path property.
	*/
	UPROPERTY()
	FName LoadedPath;
#endif
};

/**
 * Serve as a generic container of type specific data that can be assigned to each runtime cell
 */
UCLASS(Abstract)
class UWorldPartitionRuntimeCellData : public UObject
{
	GENERATED_UCLASS_BODY()
};

class UActorDescContainer;

/**
 * Cell State
 */
UENUM(BlueprintType)
enum class EWorldPartitionRuntimeCellState : uint8
{
	Unloaded,
	Loaded,
	Activated
};

static_assert(EWorldPartitionRuntimeCellState::Unloaded < EWorldPartitionRuntimeCellState::Loaded && EWorldPartitionRuntimeCellState::Loaded < EWorldPartitionRuntimeCellState::Activated, "Streaming Query code is dependent on this being true");

/**
 * Represents a PIE/Game streaming cell which points to external actor/data chunk packages
 */
UCLASS(Abstract, Within = WorldPartition)
class UWorldPartitionRuntimeCell : public UObject
{
	GENERATED_UCLASS_BODY()

	virtual void Load() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::Load,);
	virtual void Unload() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::Unload,);
	virtual void Activate() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::Activate,);
	virtual void Deactivate() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::Deactivate,);
	virtual bool IsAddedToWorld() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::IsAddedToWorld, return false;);
	virtual bool CanAddToWorld() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::CanAddToWorld, return false;);
	virtual ULevel* GetLevel() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetLevel, return nullptr;);
	virtual EWorldPartitionRuntimeCellState GetCurrentState() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetCurrentState, return EWorldPartitionRuntimeCellState::Unloaded;);
	virtual FLinearColor GetDebugColor() const { return FLinearColor::Black; }
	virtual bool IsAlwaysLoaded() const { return bIsAlwaysLoaded; }
	virtual void SetIsAlwaysLoaded(bool bInIsAlwaysLoaded) { bIsAlwaysLoaded = bInIsAlwaysLoaded; }
	virtual void SetPriority(int32 InPriority) { Priority = InPriority; }
	virtual EStreamingStatus GetStreamingStatus() const { return LEVEL_Unloaded; }
	virtual bool IsLoading() const { return false; }
	virtual const FString& GetDebugName() const { return DebugName; }
	virtual bool IsDebugShown() const;
	virtual int32 SortCompare(const UWorldPartitionRuntimeCell* Other) const;
	virtual FName GetGridName() const { return GridName; }
	/** Caches information on streaming source that will be used later on to sort cell. Returns true if cache was reset, else returns false. */
	virtual bool CacheStreamingSourceInfo(const struct FWorldPartitionStreamingSource& Source) const;

	static void DirtyStreamingSourceCacheEpoch() { ++UWorldPartitionRuntimeCell::StreamingSourceCacheEpoch; }

	bool HasDataLayers() const { return !DataLayers.IsEmpty(); }
	const TArray<FName>& GetDataLayers() const { return DataLayers; }
	bool HasAnyDataLayer(const TSet<FName>& InDataLayers) const
	{
		return Algo::AnyOf(DataLayers, [&InDataLayers](const FName& DataLayer) { return InDataLayers.Contains(DataLayer); });
	}

	bool GetBlockOnSlowLoading() const { return bBlockOnSlowLoading; }
#if WITH_EDITOR
	void SetBlockOnSlowLoading(bool bInBlockOnSlowLoading) { bBlockOnSlowLoading = bInBlockOnSlowLoading; }

	void SetClientOnlyVisible(bool bInClientOnlyVisible) { bClientOnlyVisible = bInClientOnlyVisible; }
	bool GetClientOnlyVisible() const { return bClientOnlyVisible; }

	void SetDataLayers(const TArray<const UDataLayer*>& InDataLayers);
	void SetDebugInfo(FIntVector InCoords, FName InGridName);
	void AddCellData(const UWorldPartitionRuntimeCellData* InCellData);
	virtual void AddActorToCell(const FWorldPartitionActorDescView& ActorDescView, uint64 InContainerID, const FTransform& InContainerTransform, const UActorDescContainer* InContainer) PURE_VIRTUAL(UWorldPartitionRuntimeCell::AddActorToCell,);
	virtual int32 GetActorCount() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetActorCount, return 0;);

	// Cook methods
	virtual bool PrepareCellForCook(UPackage* InPackage) { return false; }
	virtual bool PopulateGeneratedPackageForCook(UPackage* InPackage) PURE_VIRTUAL(UWorldPartitionRuntimeCell::PopulateGeneratedPackageForCook, return false;);
	virtual void MoveAlwaysLoadedContentToPersistentLevel() PURE_VIRTUAL(UWorldPartitionRuntimeCell::MoveAlwaysLoadedContentToPersistentLevel);
	virtual FString GetPackageNameToCreate() const PURE_VIRTUAL(UWorldPartitionRuntimeCell::GetPackageNameToCreate, return FString(""););
#endif

	const UWorldPartitionRuntimeCellData* GetCellData(const TSubclassOf<UWorldPartitionRuntimeCellData> InCellDataClass) const;
	template <class T> inline const T* GetCellData() const { return Cast<const T>(GetCellData(T::StaticClass())); }
	template <class T> inline bool HasCellData() const { return GetCellData<T>() != nullptr; }
	
protected:
#if WITH_EDITOR
	void UpdateDebugName();
#endif

	UPROPERTY()
	bool bIsAlwaysLoaded;

private:
	UPROPERTY()
	TMap<const TSubclassOf<UWorldPartitionRuntimeCellData>, TObjectPtr<const UWorldPartitionRuntimeCellData>> CellDataMap;

	UPROPERTY()
	TArray<FName> DataLayers;

	// Debug Info
	UPROPERTY()
	FIntVector Coords;

	UPROPERTY()
	FName GridName;

	UPROPERTY()
	FString DebugName;

	// Custom Priority
	UPROPERTY()
	int32 Priority;

	UPROPERTY()
	bool bClientOnlyVisible;

	UPROPERTY()
	bool bBlockOnSlowLoading;

	// Source Priority
	mutable int32 CachedSourcePriority;

	// Epoch used to dirty cache
	mutable int32 CachedSourceInfoEpoch;

	static int32 StreamingSourceCacheEpoch;
};
