// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldDataLayers.cpp: AWorldDataLayers class implementation
=============================================================================*/

#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "WorldPartition/WorldPartition.h"
#if WITH_EDITOR
#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#endif

#define LOCTEXT_NAMESPACE "WorldDataLayers"

int32 AWorldDataLayers::DataLayersStateEpoch = 0;

FString JoinDataLayerLabelsFromNames(AWorldDataLayers* InWorldDataLayers, const TArray<FName>& InDataLayerNames)
{
	check(InWorldDataLayers);
	TArray<FString> DataLayerLabels;
	DataLayerLabels.Reserve(InDataLayerNames.Num());
	for (const FName& DataLayerName : InDataLayerNames)
	{
		if (const UDataLayer* DataLayer = InWorldDataLayers->GetDataLayerFromName(DataLayerName))
		{
			DataLayerLabels.Add(DataLayer->GetDataLayerLabel().ToString());
		}
	}
	return FString::Join(DataLayerLabels, TEXT(","));
}

AWorldDataLayers::AWorldDataLayers(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAlwaysRelevant = true;
	bReplicates = true;

	// Avoid actor from being Destroyed/Recreated when scrubbing a replay
	// instead AWorldDataLayers::RewindForReplay() gets called to reset this actors state
	bReplayRewindable = true;
}

void AWorldDataLayers::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWorldDataLayers, RepLoadedDataLayerNames);
	DOREPLIFETIME(AWorldDataLayers, RepActiveDataLayerNames);
}

void AWorldDataLayers::BeginPlay()
{
	Super::BeginPlay();

	// When running a Replay we want to reset our state to CDO (empty) and rely on the Replay/Replication.
	// Unfortunately this can't be tested in the PostLoad as the World doesn't have a demo driver yet.
	if (GetWorld()->IsPlayingReplay())
	{
		ResetDataLayerStates();
	}
}

void AWorldDataLayers::RewindForReplay()
{
	Super::RewindForReplay();

	// Same as BeginPlay when rewinding we want to reset our state to CDO (empty) and rely on Replay/Replication.
	ResetDataLayerStates();
}

void AWorldDataLayers::InitializeDataLayerStates()
{
	check(ActiveDataLayerNames.IsEmpty() && LoadedDataLayerNames.IsEmpty());

	if (GetWorld()->IsGameWorld())
	{
		ForEachDataLayer([this](class UDataLayer* DataLayer)
		{
			if (DataLayer && DataLayer->IsDynamicallyLoaded())
			{
				if (DataLayer->GetInitialState() == EDataLayerState::Activated)
				{
					ActiveDataLayerNames.Add(DataLayer->GetFName());
				}
				else if (DataLayer->GetInitialState() == EDataLayerState::Loaded)
				{
					LoadedDataLayerNames.Add(DataLayer->GetFName());
				}
			}
			return true;
		});

		RepActiveDataLayerNames = ActiveDataLayerNames.Array();
		RepLoadedDataLayerNames = LoadedDataLayerNames.Array();

		UE_LOG(LogWorldPartition, Log, TEXT("Initial Data Layer States Activated(%s) Loaded(%s)"), *JoinDataLayerLabelsFromNames(this, RepActiveDataLayerNames), *JoinDataLayerLabelsFromNames(this, RepLoadedDataLayerNames));
	}
}

void AWorldDataLayers::ResetDataLayerStates()
{
	ActiveDataLayerNames.Reset();
	LoadedDataLayerNames.Reset();
	RepActiveDataLayerNames.Reset();
	RepLoadedDataLayerNames.Reset();
}

void AWorldDataLayers::SetDataLayerState(FActorDataLayer InDataLayer, EDataLayerState InState)
{
	if (ensure(GetLocalRole() == ROLE_Authority))
	{
		const UDataLayer* DataLayer = GetDataLayerFromName(InDataLayer.Name);
		if (!DataLayer || !DataLayer->IsDynamicallyLoaded())
		{
			return;
		}

		EDataLayerState CurrentState = GetDataLayerStateByName(InDataLayer.Name);
		if (CurrentState != InState)
		{
			LoadedDataLayerNames.Remove(InDataLayer.Name);
			ActiveDataLayerNames.Remove(InDataLayer.Name);

			if (InState == EDataLayerState::Loaded)
			{
				LoadedDataLayerNames.Add(InDataLayer.Name);
			}
			else if (InState == EDataLayerState::Activated)
			{
				ActiveDataLayerNames.Add(InDataLayer.Name);
			}

			// Update Replicated Properties
			RepActiveDataLayerNames = ActiveDataLayerNames.Array();
			RepLoadedDataLayerNames = LoadedDataLayerNames.Array();

			++DataLayersStateEpoch;

			UE_LOG(LogWorldPartition, Log, TEXT("Data Layer '%s' state changed: %s -> %s"), 
				*DataLayer->GetDataLayerLabel().ToString(), 
				*StaticEnum<EDataLayerState>()->GetDisplayNameTextByValue((int64)CurrentState).ToString(),
				*StaticEnum<EDataLayerState>()->GetDisplayNameTextByValue((int64)InState).ToString());

			OnDataLayerStateChanged(DataLayer, InState);
		}
	}
}

void AWorldDataLayers::OnDataLayerStateChanged_Implementation(const UDataLayer* InDataLayer, EDataLayerState InState)
{
	UDataLayerSubsystem* DataLayerSubsystem = GetWorld()->GetSubsystem<UDataLayerSubsystem>();
	DataLayerSubsystem->OnDataLayerStateChanged.Broadcast(InDataLayer, InState);
}

void AWorldDataLayers::OnRep_ActiveDataLayerNames()
{
	ActiveDataLayerNames.Reset();
	ActiveDataLayerNames.Append(RepActiveDataLayerNames);
}

void AWorldDataLayers::OnRep_LoadedDataLayerNames()
{
	LoadedDataLayerNames.Reset();
	LoadedDataLayerNames.Append(RepLoadedDataLayerNames);
}

EDataLayerState AWorldDataLayers::GetDataLayerStateByName(FName InDataLayerName) const
{
	if (ActiveDataLayerNames.Contains(InDataLayerName))
	{
		check(!LoadedDataLayerNames.Contains(InDataLayerName));
		return EDataLayerState::Activated;
	}
	else if (LoadedDataLayerNames.Contains(InDataLayerName))
	{
		check(!ActiveDataLayerNames.Contains(InDataLayerName));
		return EDataLayerState::Loaded;
	}

	return EDataLayerState::Unloaded;
}

#if WITH_EDITOR
void AWorldDataLayers::OverwriteDataLayerStates(TArray<FActorDataLayer>* InActiveDataLayers, TArray<FActorDataLayer>* InLoadedDataLayers)
{
	if (GetLocalRole() == ROLE_Authority)
	{
		// This should get called before game starts. It doesn't send out events
		check(!GetWorld()->bMatchStarted);
		if (InActiveDataLayers)
		{
			ActiveDataLayerNames.Empty(InActiveDataLayers->Num());
			for (const FActorDataLayer& DataLayer : *InActiveDataLayers)
			{
				ActiveDataLayerNames.Add(DataLayer.Name);
			}
			RepActiveDataLayerNames = ActiveDataLayerNames.Array();
		}

		if (InLoadedDataLayers)
		{
			LoadedDataLayerNames.Empty(InLoadedDataLayers->Num());
			for (const FActorDataLayer& DataLayer : *InLoadedDataLayers)
			{
				LoadedDataLayerNames.Add(DataLayer.Name);
			}
			RepLoadedDataLayerNames = LoadedDataLayerNames.Array();
		}

		UE_LOG(LogWorldPartition, Log, TEXT("Overwrite Data Layer States Activated(%s) Loaded(%s)"), *JoinDataLayerLabelsFromNames(this, RepActiveDataLayerNames), *JoinDataLayerLabelsFromNames(this, RepLoadedDataLayerNames));
	}
}

AWorldDataLayers* AWorldDataLayers::Create(UWorld* World)
{
	check(World);
	check(!World->GetWorldDataLayers());

	AWorldDataLayers* WorldDataLayers = nullptr;

	static FName WorldDataLayersName = AWorldDataLayers::StaticClass()->GetFName();
	if (UObject* ExistingObject = StaticFindObject(nullptr, World->PersistentLevel, *WorldDataLayersName.ToString()))
	{
		WorldDataLayers = CastChecked<AWorldDataLayers>(ExistingObject);
		if (WorldDataLayers->IsPendingKill())
		{
			// Handle the case where the actor already exists, but it's pending kill
			WorldDataLayers->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional | REN_ForceNoResetLoaders);
			WorldDataLayers = nullptr;
		}
	}

	if (!WorldDataLayers)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.OverrideLevel = World->PersistentLevel;
		SpawnParams.bHideFromSceneOutliner = true;
		SpawnParams.Name = WorldDataLayersName;
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_Fatal;
		WorldDataLayers = World->SpawnActor<AWorldDataLayers>(AWorldDataLayers::StaticClass(), SpawnParams);
	}

	check(WorldDataLayers);

	World->Modify();
	World->SetWorldDataLayers(WorldDataLayers);

	return WorldDataLayers;
}

FName AWorldDataLayers::GenerateUniqueDataLayerLabel(const FName& InDataLayerLabel) const
{
	int32 DataLayerIndex = 0;
	const FName DataLayerLabelSanitized = UDataLayer::GetSanitizedDataLayerLabel(InDataLayerLabel);
	FName UniqueNewDataLayerLabel = DataLayerLabelSanitized;
	while (GetDataLayerFromLabel(UniqueNewDataLayerLabel))
	{
		UniqueNewDataLayerLabel = FName(*FString::Printf(TEXT("%s%d"), *DataLayerLabelSanitized.ToString(), ++DataLayerIndex));
	};
	return UniqueNewDataLayerLabel;
}

TArray<FName> AWorldDataLayers::GetDataLayerNames(const TArray<FActorDataLayer>& InDataLayers) const
{
	TArray<FName> OutDataLayerNames;
	OutDataLayerNames.Reserve(DataLayers.Num());

	for (const UDataLayer* DataLayer : GetDataLayerObjects(InDataLayers))
	{
		OutDataLayerNames.Add(DataLayer->GetFName());
	}

	return OutDataLayerNames;
}

TArray<const UDataLayer*> AWorldDataLayers::GetDataLayerObjects(const TArray<FActorDataLayer>& InDataLayers) const
{
	TArray<const UDataLayer*> OutDataLayers;
	OutDataLayers.Reserve(DataLayers.Num());

	for (const FActorDataLayer& DataLayer : InDataLayers)
	{
		if (const UDataLayer* DataLayerObject = GetDataLayerFromName(DataLayer.Name))
		{
			OutDataLayers.AddUnique(DataLayerObject);
		}
	}

	return OutDataLayers;
}

UDataLayer* AWorldDataLayers::CreateDataLayer(FName InName, EObjectFlags InObjectFlags)
{
	Modify();

	// Make sure new DataLayer name (not label) is unique and never re-used so that actors still referencing on deleted DataLayer's don't get valid again.
	const FName DataLayerUniqueName = *FString::Format(TEXT("DataLayer_{0}"), { FGuid::NewGuid().ToString()});
	UDataLayer* NewDataLayer = NewObject<UDataLayer>(this, DataLayerUniqueName, RF_Transactional | InObjectFlags);
	check(NewDataLayer != NULL);
	FName DataLayerLabel = GenerateUniqueDataLayerLabel(InName);
	NewDataLayer->SetDataLayerLabel(DataLayerLabel);
	NewDataLayer->SetVisible(true);
	WorldDataLayers.Add(NewDataLayer);
	check(GetDataLayerFromName(NewDataLayer->GetFName()));
	return NewDataLayer;
}

bool AWorldDataLayers::RemoveDataLayers(const TArray<UDataLayer*>& InDataLayers)
{
	bool bIsModified = false;
	for (const UDataLayer* DataLayer : InDataLayers)
	{
		if (ContainsDataLayer(DataLayer))
		{
			Modify();
			WorldDataLayers.Remove(const_cast<UDataLayer*>(DataLayer));
			bIsModified = true;
		}
	}
	return bIsModified;
}

bool AWorldDataLayers::RemoveDataLayer(UDataLayer* InDataLayer)
{
	if (ContainsDataLayer(InDataLayer))
	{
		Modify();
		WorldDataLayers.Remove(const_cast<UDataLayer*>(InDataLayer));
		return true;
	}
	return false;
}

#endif

bool AWorldDataLayers::ContainsDataLayer(const UDataLayer* InDataLayer) const
{
	return WorldDataLayers.Contains(InDataLayer);
}

const UDataLayer* AWorldDataLayers::GetDataLayerFromName(const FName& InDataLayerName) const
{
#if WITH_EDITOR	
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		if (DataLayer->GetFName() == InDataLayerName)
		{
			return DataLayer;
		}
	}
#else
	if (const UDataLayer* const* FoundDataLayer = NameToDataLayer.Find(InDataLayerName))
	{
		return *FoundDataLayer;
	}
#endif
	return nullptr;
}

const UDataLayer* AWorldDataLayers::GetDataLayerFromLabel(const FName& InDataLayerLabel) const
{
	const FName DataLayerLabelSanitized = UDataLayer::GetSanitizedDataLayerLabel(InDataLayerLabel);
#if WITH_EDITOR	
	for (const UDataLayer* DataLayer : WorldDataLayers)
	{
		if (DataLayer->GetDataLayerLabel() == DataLayerLabelSanitized)
		{
			return DataLayer;
		}
	}
#else
	if (const UDataLayer* const* FoundDataLayer = LabelToDataLayer.Find(DataLayerLabelSanitized))
	{
		return *FoundDataLayer;
	}
#endif
	return nullptr;
}

void AWorldDataLayers::ForEachDataLayer(TFunctionRef<bool(UDataLayer*)> Func)
{
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		if (!Func(DataLayer))
		{
			break;
		}
	}
}

void AWorldDataLayers::ForEachDataLayer(TFunctionRef<bool(UDataLayer*)> Func) const
{
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		if (!Func(DataLayer))
		{
			break;
		}
	}
}

void AWorldDataLayers::PostLoad()
{
	Super::PostLoad();

	GetLevel()->ConditionalPostLoad();

	GetWorld()->SetWorldDataLayers(this);

#if WITH_EDITOR
	// Setup defaults before overriding with user settings
	for (UDataLayer* DataLayer : WorldDataLayers)
	{
		DataLayer->SetIsDynamicallyLoadedInEditor(DataLayer->IsInitiallyLoadedInEditor());
	}

	// Initialize DataLayer's IsDynamicallyLoadedInEditor based on DataLayerEditorPerProjectUserSettings
	const TArray<FName>& SettingsDataLayersNotLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersNotLoadedInEditor(GetWorld());
	for (const FName& DataLayerName : SettingsDataLayersNotLoadedInEditor)
	{
		if (UDataLayer* DataLayer = const_cast<UDataLayer*>(GetDataLayerFromName(DataLayerName)))
		{
			DataLayer->SetIsDynamicallyLoadedInEditor(false);
		}
	}

	const TArray<FName>& SettingsDataLayersLoadedInEditor = GetMutableDefault<UWorldPartitionEditorPerProjectUserSettings>()->GetWorldDataLayersLoadedInEditor(GetWorld());
	for (const FName& DataLayerName : SettingsDataLayersLoadedInEditor)
	{
		if (UDataLayer* DataLayer = const_cast<UDataLayer*>(GetDataLayerFromName(DataLayerName)))
		{
			DataLayer->SetIsDynamicallyLoadedInEditor(true);
}
	}
#else
	// Build acceleration tables
	for (const UDataLayer* DataLayer : WorldDataLayers)
	{
		LabelToDataLayer.Add(DataLayer->GetDataLayerLabel(), DataLayer);
		NameToDataLayer.Add(DataLayer->GetFName(), DataLayer);
	}
#endif

	InitializeDataLayerStates();
}

#undef LOCTEXT_NAMESPACE