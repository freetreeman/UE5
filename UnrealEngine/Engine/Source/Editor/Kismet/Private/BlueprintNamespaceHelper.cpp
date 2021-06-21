// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintNamespaceHelper.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorSettings.h"
#include "Settings/EditorProjectSettings.h"
#include "Toolkits/ToolkitManager.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "AssetRegistryModule.h"
#include "SPinTypeSelector.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ClassViewerFilter.h"

#define LOCTEXT_NAMESPACE "BlueprintNamespaceHelper"

// ---
// @todo_namespaces - Remove CVar flags/sink below after converting to editable 'config' properties
// ---

static TAutoConsoleVariable<bool> CVarBPEnableNamespaceFilteringFeatures(
	TEXT("BP.EnableNamespaceFilteringFeatures"),
	false,
	TEXT("Enables namespace filtering features in the Blueprint editor (experimental).")
);

static TAutoConsoleVariable<bool> CVarBPEnableNamespaceImportingFeatures(
	TEXT("BP.EnableNamespaceImportingFeatures"),
	false,
	TEXT("Enables namespace importing features in the Blueprint editor (experimental)."));

static TAutoConsoleVariable<bool> CVarBPImportParentClassNamespaces(
	TEXT("BP.ImportParentClassNamespaces"),
	false,
	TEXT("Enables import of parent class namespaces when opening a Blueprint for editing."));

static void UpdateNamespaceFeatureSettingsCVarSinkFunction()
{
	// Note: Do NOT try to access settings objects below during the initial editor load! They rely on the config being loaded, which may not have occurred yet.
	if (GIsInitialLoad || !GEditor)
	{
		return;
	}

	auto CheckAndUpdateSettingValueLambda = [](bool& CurValue, const bool NewValue) -> bool
	{
		if (CurValue != NewValue)
		{
			CurValue = NewValue;
			return true;
		}

		return false;
	};

	bool bWasUpdated = false;

	// Blueprint editor settings.
	UBlueprintEditorSettings* BlueprintEditorSettingsPtr = GetMutableDefault<UBlueprintEditorSettings>();
	bWasUpdated |= CheckAndUpdateSettingValueLambda(BlueprintEditorSettingsPtr->bEnableNamespaceFilteringFeatures, CVarBPEnableNamespaceFilteringFeatures.GetValueOnGameThread());
	bWasUpdated |= CheckAndUpdateSettingValueLambda(BlueprintEditorSettingsPtr->bEnableNamespaceImportingFeatures, CVarBPEnableNamespaceImportingFeatures.GetValueOnGameThread());

	if (bWasUpdated)
	{
		// Refresh all relevant open Blueprint editor UI elements.
		// @todo_namespaces - Move this into PostEditChangeProperty() on the appropriate settings object(s).
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				if (Asset && Asset->IsA<UBlueprint>())
				{
					TSharedPtr<IToolkit> AssetEditorPtr = FToolkitManager::Get().FindEditorForAsset(Asset);
					if (AssetEditorPtr.IsValid() && AssetEditorPtr->IsBlueprintEditor())
					{
						TSharedPtr<IBlueprintEditor> BlueprintEditorPtr = StaticCastSharedPtr<IBlueprintEditor>(AssetEditorPtr);
						BlueprintEditorPtr->RefreshMyBlueprint();
						BlueprintEditorPtr->RefreshInspector();
					}
				}
			}
		}
	}
}

static FAutoConsoleVariableSink CVarUpdateNamespaceFeatureSettingsSink(
	FConsoleCommandDelegate::CreateStatic(&UpdateNamespaceFeatureSettingsCVarSinkFunction)
);

// ---

class FClassViewerNamespaceFilter : public IClassViewerFilter
{
public:
	FClassViewerNamespaceFilter(const FBlueprintNamespaceHelper* InNamespaceHelper)
		: CachedNamespaceHelper(InNamespaceHelper)
	{
	}

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		if (!CachedNamespaceHelper)
		{
			return true;
		}

		return CachedNamespaceHelper->IsImportedObject(InClass);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InBlueprint, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		if (!CachedNamespaceHelper)
		{
			return true;
		}

		FSoftObjectPath ClassPath(InBlueprint->GetClassPath());
		return CachedNamespaceHelper->IsImportedObject(ClassPath);
	}

private:
	/** Associated namespace helper object. */
	const FBlueprintNamespaceHelper* CachedNamespaceHelper;
};

// ---

class FPinTypeSelectorNamespaceFilter : public IPinTypeSelectorFilter
{
	DECLARE_MULTICAST_DELEGATE(FOnFilterChanged);

public:
	FPinTypeSelectorNamespaceFilter(const FBlueprintNamespaceHelper* InNamespaceHelper)
		: CachedNamespaceHelper(InNamespaceHelper)
	{
	}

	virtual bool ShouldShowPinTypeTreeItem(FPinTypeTreeItem InItem) const override
	{
		if (!CachedNamespaceHelper)
		{
			return true;
		}

		const bool bForceLoadSubCategoryObject = false;
		const FEdGraphPinType& PinType = InItem->GetPinType(bForceLoadSubCategoryObject);

		if (PinType.PinSubCategoryObject.IsValid() && !CachedNamespaceHelper->IsImportedObject(PinType.PinSubCategoryObject.Get()))
		{
			// A pin type whose underlying object is loaded, but not imported.
			return false;
		}
		else
		{
			const FSoftObjectPath& AssetRef = InItem->GetSubCategoryObjectAsset();
			if (AssetRef.IsValid() && !CachedNamespaceHelper->IsImportedObject(AssetRef))
			{
				// A pin type whose underlying asset may be either loaded or unloaded, but is not imported.
				return false;
			}
		}

		return true;
	}

private:
	/** Associated namespace helper object. */
	const FBlueprintNamespaceHelper* CachedNamespaceHelper;
};

// ---

FBlueprintNamespaceHelper::FBlueprintNamespaceHelper(const UBlueprint* InBlueprint)
{
	// Default namespace paths implicitly imported by every Blueprint.
	AddNamespaces(GetDefault<UBlueprintEditorSettings>()->NamespacesToAlwaysInclude);
	AddNamespaces(GetDefault<UBlueprintEditorProjectSettings>()->NamespacesToAlwaysInclude);

	if (InBlueprint)
	{
		AddNamespace(InBlueprint->BlueprintNamespace);
		AddNamespaces(InBlueprint->ImportedNamespaces);

		const bool bAddParentClassNamespaces = CVarBPImportParentClassNamespaces.GetValueOnGameThread();
		if(bAddParentClassNamespaces)
		{
			const UClass* ParentClass = InBlueprint->ParentClass;
			while (ParentClass)
			{
				if (const UBlueprint* ParentClassBlueprint = UBlueprint::GetBlueprintFromClass(ParentClass))
				{
					AddNamespace(ParentClassBlueprint->BlueprintNamespace);
					AddNamespaces(ParentClassBlueprint->ImportedNamespaces);
				}
				else if (const FString* ParentClassNamespace = ParentClass->FindMetaData(FBlueprintMetadata::MD_Namespace))
				{
					AddNamespace(*ParentClassNamespace);
				}

				ParentClass = ParentClass->GetSuperClass();
			}
		}
	}

	ClassViewerFilter = MakeShared<FClassViewerNamespaceFilter>(this);
	PinTypeSelectorFilter = MakeShared<FPinTypeSelectorNamespaceFilter>(this);
}

bool FBlueprintNamespaceHelper::IsIncludedInNamespaceList(const FString& TestNamespace) const
{
	// Empty namespace == global namespace
	if (TestNamespace.IsEmpty())
	{
		return true;
	}

	// Check recursively to see if X.Y.Z is present, and if not X.Y (which contains X.Y.Z), and so on until we run out of path segments
	if (FullyQualifiedListOfNamespaces.Contains(TestNamespace))
	{
		return true;
	}
	else
	{
		int32 RightmostDotIndex;
		if (TestNamespace.FindLastChar(TEXT('.'), /*out*/ RightmostDotIndex))
		{
			if (RightmostDotIndex > 0)
			{
				return IsIncludedInNamespaceList(TestNamespace.Left(RightmostDotIndex));
			}
		}
	}

	return false;
}

bool FBlueprintNamespaceHelper::IsImportedType(const UField* InType) const
{
	if (InType)
	{
		if (const FString* TypeNamespace = InType->FindMetaData(FBlueprintMetadata::MD_Namespace))
		{
			return IsIncludedInNamespaceList(*TypeNamespace);
		}
	}

	// Types exist in the global scope if we can't determine otherwise, which means it's always imported.
	return true;
}

bool FBlueprintNamespaceHelper::IsImportedObject(const UObject* InObject) const
{
	if (const UField* Type = Cast<UField>(InObject))
	{
		return IsImportedType(Type);
	}
	else
	{
		return IsImportedType(InObject->GetClass());
	}
}

bool FBlueprintNamespaceHelper::IsImportedObject(const FSoftObjectPath& InObjectPath) const
{
	if (const UObject* Object = InObjectPath.ResolveObject())
	{
		return IsImportedObject(Object);
	}
	
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*InObjectPath.ToString());
	if (AssetData.IsValid())
	{
		if (const UClass* AssetClass = FindObject<UClass>(ANY_PACKAGE, *AssetData.AssetClass.ToString()))
		{
			if (AssetClass->IsChildOf<UBlueprint>())
			{
				FString OutNamespaceString;
				if (AssetData.GetTagValue<FString>(GET_MEMBER_NAME_STRING_CHECKED(UBlueprint, BlueprintNamespace), OutNamespaceString))
				{
					return IsIncludedInNamespaceList(OutNamespaceString);
				}
			}

			// @todo_namespaces - Add cases for unloaded UDS/UDE assets once they have a searchable namespace member property.
		}
	}

	// Objects exist in the global scope if we can't determine otherwise, which means it's always imported.
	return true;
}

#undef LOCTEXT_NAMESPACE