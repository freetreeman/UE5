// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundUObjectRegistry.h"

#include "Algo/Copy.h"
#include "AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundSource.h"
#include "UObject/Object.h"


namespace Metasound
{
	namespace AssetSubsystemPrivate
	{
		static bool GetAssetClassInfo(const FAssetData& InAssetData, Frontend::FNodeClassInfo& OutInfo)
		{
			using namespace Metasound;
			using namespace Metasound::Frontend;

			bool bSuccess = true;

			OutInfo.Type = EMetasoundFrontendClassType::External;
			OutInfo.AssetPath = InAssetData.ObjectPath;

			FString AssetClassID;
			bSuccess &= InAssetData.GetTagValue(AssetTags::AssetClassID, AssetClassID);
			OutInfo.AssetClassID = FGuid(AssetClassID);
			OutInfo.ClassName = FMetasoundFrontendClassName(FName(), *AssetClassID, FName());

			int32 RegistryVersionMajor = 0;
			bSuccess &= InAssetData.GetTagValue(AssetTags::RegistryVersionMajor, RegistryVersionMajor);
			OutInfo.Version.Major = RegistryVersionMajor;

			int32 RegistryVersionMinor = 0;
			bSuccess &= InAssetData.GetTagValue(AssetTags::RegistryVersionMinor, RegistryVersionMinor);
			OutInfo.Version.Minor = RegistryVersionMinor;

#if WITH_EDITORONLY_DATA
			auto ParseTypesString = [&](const FName AssetTag, TArray<FName>& OutTypes)
			{
				FString TypesString;
				if (InAssetData.GetTagValue(AssetTag, TypesString))
				{
					TArray<FString> DataTypeStrings;
					TypesString.ParseIntoArray(DataTypeStrings, *AssetTags::ArrayDelim);
					Algo::Transform(DataTypeStrings, OutTypes, [](const FString& DataType) { return *DataType; });
					return true;
				}

				return false;
			};

			OutInfo.InputTypes.Reset();
			bSuccess &= ParseTypesString(AssetTags::RegistryInputTypes, OutInfo.InputTypes);

			OutInfo.OutputTypes.Reset();
			bSuccess &= ParseTypesString(AssetTags::RegistryOutputTypes, OutInfo.OutputTypes);
#endif // WITH_EDITORONLY_DATA

			return bSuccess;
		}
	}
}

void UMetaSoundAssetSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
//  TODO: Enable Composition
//	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UMetaSoundAssetSubsystem::PostEngineInit);
}

void UMetaSoundAssetSubsystem::PostEngineInit()
{
	if (UAssetManager* AssetManager = UAssetManager::GetIfValid())
	{
		AssetManager->CallOrRegister_OnCompletedInitialScan(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UMetaSoundAssetSubsystem::PostInitAssetScan));
	}
	else
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot initialize MetaSoundAssetSubsystem: Enable AssetManager or disable MetaSound plugin"));
	}
}

void UMetaSoundAssetSubsystem::PostInitAssetScan()
{
	UAssetManager& AssetManager = UAssetManager::Get();

	FAssetManagerSearchRules Rules;
	Rules.AssetScanPaths.Add(TEXT("/Game"));

	Rules.AssetBaseClass = UMetaSound::StaticClass();
	TArray<FAssetData> MetaSoundAssets;
	AssetManager.SearchAssetRegistryPaths(MetaSoundAssets, Rules);
	for (const FAssetData& AssetData : MetaSoundAssets)
	{
		AddOrUpdateAsset(AssetData);
	}

	Rules.AssetBaseClass = UMetaSoundSource::StaticClass();
	TArray<FAssetData> MetaSoundSourceAssets;
	AssetManager.SearchAssetRegistryPaths(MetaSoundSourceAssets, Rules);
	for (const FAssetData& AssetData : MetaSoundSourceAssets)
	{
		AddOrUpdateAsset(AssetData);
	}
}

void UMetaSoundAssetSubsystem::Deinitialize()
{
}

void UMetaSoundAssetSubsystem::AddOrUpdateAsset(const FAssetData& InAssetData)
{
	using namespace Metasound;
	using namespace Metasound::AssetSubsystemPrivate;
	using namespace Metasound::Frontend;

	// TODO: Set to false for builds once registering without loading asset is supported.
	static const bool bLoadRequiredToRegisterAssetClasses = true;

	FNodeClassInfo ClassInfo;
	bool bClassInfoFound = GetAssetClassInfo(InAssetData, ClassInfo);
	if (!bClassInfoFound || bLoadRequiredToRegisterAssetClasses)
	{
		UObject* Object = nullptr;

		FSoftObjectPath Path(InAssetData.ObjectPath);
		if (InAssetData.IsAssetLoaded())
		{
			Object = Path.ResolveObject();
		}
		else
		{
			if (!bLoadRequiredToRegisterAssetClasses)
			{
				UE_LOG(LogMetaSound, Warning,
					TEXT("Failed to find serialized MetaSound asset registry data for asset '%s'. "
						"Forcing synchronous load which increases load times. Re-save asset to avoid this."),
					*InAssetData.ObjectPath.ToString());
			}
			Object = Path.TryLoad();
		}

		if (ensure(Object))
		{
			FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
			check(MetaSoundAsset);

			FDocumentHandle Document = MetaSoundAsset->GetDocumentHandle();

			// Must version to ensure registration uses the correct key,
			// which must be based off of most up-to-date document model
			// for safety.
			const FName AssetName = Object->GetFName();
			const FString AssetPath = Object->GetPathName();
			FVersionDocument(AssetName, AssetPath).Transform(Document);

			MetaSoundAsset->RegisterGraphWithFrontend();
		}
	}
}

void UMetaSoundAssetSubsystem::RemoveAsset(const FAssetData& InAssetData)
{
	using namespace Metasound;
	using namespace Metasound::AssetSubsystemPrivate;
	using namespace Metasound::Frontend;

	FNodeClassInfo ClassInfo;
	if (!GetAssetClassInfo(InAssetData, ClassInfo))
	{
		UObject* Object = nullptr;

		FSoftObjectPath Path(InAssetData.ObjectPath);
		if (InAssetData.IsAssetLoaded())
		{
			Object = Path.ResolveObject();
		}
		else
		{
			Object = Path.TryLoad();
		}

		if (ensure(Object))
		{
			FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
			check(MetaSoundAsset);

			FDocumentHandle Document = MetaSoundAsset->GetDocumentHandle();

			// Must version to ensure registration uses the correct key,
			// which must be based off of most up-to-date document model
			// for safety.
			const FName AssetName = Object->GetFName();
			const FString AssetPath = Object->GetPathName();
			FVersionDocument(AssetName, AssetPath).Transform(Document);

			ClassInfo = MetaSoundAsset->GetAssetClassInfo();
		}
	}

	const FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassInfo);
	FMetasoundFrontendRegistryContainer::Get()->UnregisterNode(RegistryKey);
}

namespace Metasound
{

	class FMetasoundUObjectRegistry : public IMetasoundUObjectRegistry
	{
		public:
			void RegisterUClassArchetype(TUniquePtr<IMetasoundUObjectRegistryEntry>&& InEntry) override
			{
				if (InEntry.IsValid())
				{
					FName ArchetypeName = InEntry->GetArchetypeName();

					EntriesByArchetype.Add(ArchetypeName, InEntry.Get());
					Entries.Add(InEntry.Get());
					Storage.Add(MoveTemp(InEntry));
				}
			}

			TArray<UClass*> GetUClassesForArchetype(const FName& InArchetypeName) const override
			{
				TArray<UClass*> Classes;

				TArray<const IMetasoundUObjectRegistryEntry*> EntriesForArchetype;
				EntriesByArchetype.MultiFind(InArchetypeName, EntriesForArchetype);

				for (const IMetasoundUObjectRegistryEntry* Entry : EntriesForArchetype)
				{
					if (nullptr != Entry)
					{
						if (UClass* Class = Entry->GetUClass())
						{
							Classes.Add(Class);
						}
					}
				}

				return Classes;
			}

			UObject* NewObject(UClass* InClass, const FMetasoundFrontendDocument& InDocument, const FMetasoundFrontendArchetype& InArchetype, const FString& InPath) const override
			{
				auto IsChildClassOfRegisteredClass = [&](const IMetasoundUObjectRegistryEntry* Entry)
				{
					return Entry->IsChildClass(InClass);
				};

				TArray<const IMetasoundUObjectRegistryEntry*> EntriesForClass = FindEntriesByPredicate(IsChildClassOfRegisteredClass);

				for (const IMetasoundUObjectRegistryEntry* Entry : EntriesForClass)
				{
					if (Entry->GetArchetypeName() == InArchetype.Name)
					{
						return NewObject(*Entry, InDocument, InPath);
					}
				}

				return nullptr;
			}

			bool IsRegisteredClass(UObject* InObject) const override
			{
				return (nullptr != GetEntryByUObject(InObject));
			}

			FMetasoundAssetBase* GetObjectAsAssetBase(UObject* InObject) const override
			{
				if (const IMetasoundUObjectRegistryEntry* Entry = GetEntryByUObject(InObject))
				{
					return Entry->Cast(InObject);
				}
				return nullptr;
			}

			const FMetasoundAssetBase* GetObjectAsAssetBase(const UObject* InObject) const override
			{
				if (const IMetasoundUObjectRegistryEntry* Entry = GetEntryByUObject(InObject))
				{
					return Entry->Cast(InObject);
				}
				return nullptr;
			}

		private:
			UObject* NewObject(const IMetasoundUObjectRegistryEntry& InEntry, const FMetasoundFrontendDocument& InDocument, const FString& InPath) const
			{
				UPackage* PackageToSaveTo = nullptr;

				if (GIsEditor)
				{
					FText InvalidPathReason;
					bool const bValidPackageName = FPackageName::IsValidLongPackageName(InPath, false, &InvalidPathReason);

					if (!ensureAlwaysMsgf(bValidPackageName, TEXT("Tried to generate a Metasound UObject with an invalid package path/name Falling back to transient package, which means we won't be able to save this asset.")))
					{
						PackageToSaveTo = GetTransientPackage();
					}
					else
					{
						PackageToSaveTo = CreatePackage(*InPath);
					}
				}
				else
				{
					PackageToSaveTo = GetTransientPackage();
				}

				UObject* NewMetasoundObject = InEntry.NewObject(PackageToSaveTo, *InDocument.RootGraph.Metadata.ClassName.GetFullName().ToString());
				FMetasoundAssetBase* NewAssetBase = InEntry.Cast(NewMetasoundObject);
				if (ensure(nullptr != NewAssetBase))
				{
					NewAssetBase->SetDocument(InDocument);

					const FMetasoundFrontendArchetype& Archetype = NewAssetBase->GetArchetype();
					if (ensure(NewAssetBase->IsArchetypeSupported(Archetype)))
					{
						NewAssetBase->ConformDocumentToArchetype();
					}
				}

#if WITH_EDITOR
				AsyncTask(ENamedThreads::GameThread, [NewMetasoundObject]()
				{
					FAssetRegistryModule::AssetCreated(NewMetasoundObject);
					NewMetasoundObject->MarkPackageDirty();
					// todo: how do you get the package for a uobject and save it? I forget
				});
#endif

				return NewMetasoundObject;
			}

			const IMetasoundUObjectRegistryEntry* FindEntryByPredicate(TFunction<bool (const IMetasoundUObjectRegistryEntry*)> InPredicate) const
			{
				const IMetasoundUObjectRegistryEntry* const* Entry = Entries.FindByPredicate(InPredicate);

				if (nullptr == Entry)
				{
					return nullptr;
				}

				return *Entry;
			}

			TArray<const IMetasoundUObjectRegistryEntry*> FindEntriesByPredicate(TFunction<bool (const IMetasoundUObjectRegistryEntry*)> InPredicate) const
			{
				TArray<const IMetasoundUObjectRegistryEntry*> FoundEntries;

				Algo::CopyIf(Entries, FoundEntries, InPredicate);

				return FoundEntries;
			}

			const IMetasoundUObjectRegistryEntry* GetEntryByUObject(const UObject* InObject) const
			{
				auto IsChildClassOfRegisteredClass = [&](const IMetasoundUObjectRegistryEntry* Entry)
				{
					if (nullptr == Entry)
					{
						return false;
					}

					return Entry->IsChildClass(InObject);
				};

				return FindEntryByPredicate(IsChildClassOfRegisteredClass);
			}

			TArray<TUniquePtr<IMetasoundUObjectRegistryEntry>> Storage;
			TMultiMap<FName, const IMetasoundUObjectRegistryEntry*> EntriesByArchetype;
			TArray<const IMetasoundUObjectRegistryEntry*> Entries;
	};

	IMetasoundUObjectRegistry& IMetasoundUObjectRegistry::Get()
	{
		static FMetasoundUObjectRegistry Registry;
		return Registry;
	}
}
