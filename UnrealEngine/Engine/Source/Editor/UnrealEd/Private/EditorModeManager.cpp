// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "Misc/MessageDialog.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/WorldSettings.h"
#include "LevelEditorViewport.h"
#include "EditorModeRegistry.h"
#include "EditorModes.h"
#include "EditorSupportDelegates.h"
#include "EdMode.h"
#include "Toolkits/IToolkitHost.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SButton.h"
#include "Engine/LevelStreaming.h"
#include "Editor/EditorEngine.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Toolkits/BaseToolkit.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/BrushEditingSubsystem.h"
#include "Tools/UEdMode.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "InputRouter.h"
#include "InteractiveGizmoManager.h"
#include "EdModeInteractiveToolsContext.h"
#include "Tools/LegacyEdModeInterfaces.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Tools/AssetEditorContextObject.h"
#include "ContextObjectStore.h"

/*------------------------------------------------------------------------------
	FEditorModeTools.

	The master class that handles tracking of the current mode.
------------------------------------------------------------------------------*/

const FName FEditorModeTools::EditorModeToolbarTabName = TEXT("EditorModeToolbar");

FEditorModeTools::FEditorModeTools()
	: PivotShown(false)
	, Snapping(false)
	, SnappedActor(false)
	, CachedLocation(ForceInitToZero)
	, PivotLocation(ForceInitToZero)
	, SnappedLocation(ForceInitToZero)
	, GridBase(ForceInitToZero)
	, TranslateRotateXAxisAngle(0.0f)
	, TranslateRotate2DAngle(0.0f)
	, DefaultModeIDs()
	, WidgetMode(UE::Widget::WM_None)
	, OverrideWidgetMode(UE::Widget::WM_None)
	, bShowWidget(true)
	, bHideViewportUI(false)
	, bSelectionHasSceneComponent(false)
	, WidgetScale(1.0f)
	, CoordSystem(COORD_World)
	, bIsTracking(false)
{
	DefaultModeIDs.Add( FBuiltinEditorModes::EM_Default );

	InteractiveToolsContext = NewObject<UEdModeInteractiveToolsContext>(GetTransientPackage(), UEdModeInteractiveToolsContext::StaticClass(), NAME_None, RF_Transient);
	InteractiveToolsContext->InitializeContextWithEditorModeManager(this);

	// Load the last used settings
	LoadConfig();

	// Register our callback for actor selection changes
	USelection::SelectNoneEvent.AddRaw(this, &FEditorModeTools::OnEditorSelectNone);
	USelection::SelectionChangedEvent.AddRaw(this, &FEditorModeTools::OnEditorSelectionChanged);
	USelection::SelectObjectEvent.AddRaw(this, &FEditorModeTools::OnEditorSelectionChanged);

	if( GEditor )
	{
		// Register our callback for undo/redo
		GEditor->RegisterForUndo(this);

		// This binding ensures the mode is destroyed if the type is unregistered outside of normal shutdown process
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModeUnregistered().AddRaw(this, &FEditorModeTools::OnModeUnregistered);
	}

	FWorldDelegates::OnWorldCleanup.AddRaw(this, &FEditorModeTools::OnWorldCleanup);
}

FEditorModeTools::~FEditorModeTools()
{
	RemoveAllDelegateHandlers();

	SetDefaultMode(FBuiltinEditorModes::EM_Default);
	DeactivateAllModes();
	DeactivateAllModesPendingDeletion();
	RecycledScriptableModes.Empty();

	// We may be destroyed after the UObject system has already shutdown, 
	// which would mean that this instances will be garbage
	if (UObjectInitialized())
	{
		InteractiveToolsContext->ShutdownContext();
		InteractiveToolsContext = nullptr;
	}
}

void FEditorModeTools::LoadConfig(void)
{
	GConfig->GetBool(TEXT("FEditorModeTools"),TEXT("ShowWidget"),bShowWidget,
		GEditorPerProjectIni);

	const bool bGetRawValue = true;
	int32 CoordSystemAsInt = (int32)GetCoordSystem(bGetRawValue);
	GConfig->GetInt(TEXT("FEditorModeTools"),TEXT("CoordSystem"), CoordSystemAsInt,
		GEditorPerProjectIni);
	SetCoordSystem((ECoordSystem)CoordSystemAsInt);

	LoadWidgetSettings();
}

void FEditorModeTools::SaveConfig(void)
{
	GConfig->SetBool(TEXT("FEditorModeTools"), TEXT("ShowWidget"), bShowWidget, GEditorPerProjectIni);

	const bool bGetRawValue = true;
	GConfig->SetInt(TEXT("FEditorModeTools"), TEXT("CoordSystem"), (int32)GetCoordSystem(bGetRawValue), GEditorPerProjectIni);

	SaveWidgetSettings();
}

TSharedPtr<class IToolkitHost> FEditorModeTools::GetToolkitHost() const
{
	TSharedPtr<class IToolkitHost> Result = ToolkitHost.Pin();
	check(ToolkitHost.IsValid());
	return Result;
}

bool FEditorModeTools::HasToolkitHost() const
{
	return ToolkitHost.Pin().IsValid();
}

void FEditorModeTools::SetToolkitHost(TSharedRef<class IToolkitHost> InHost)
{
	checkf(!ToolkitHost.IsValid(), TEXT("SetToolkitHost can only be called once"));
	ToolkitHost = InHost;

	if (HasToolkitHost())
	{
		UAssetEditorContextObject* AssetEditorContextObject = NewObject<UAssetEditorContextObject>(InteractiveToolsContext->ToolManager);
		AssetEditorContextObject->SetToolkitHost(GetToolkitHost().Get());
		InteractiveToolsContext->ContextObjectStore->AddContextObject(AssetEditorContextObject);
	}
}

USelection* FEditorModeTools::GetSelectedActors() const
{
	return GEditor->GetSelectedActors();
}

USelection* FEditorModeTools::GetSelectedObjects() const
{
	return GEditor->GetSelectedObjects();
}

USelection* FEditorModeTools::GetSelectedComponents() const
{
	return GEditor->GetSelectedComponents();
}

UTypedElementSelectionSet* FEditorModeTools::GetEditorSelectionSet() const
{
	return GetSelectedActors()->GetElementSelectionSet();
}

void FEditorModeTools::StoreSelection(FName SelectionStoreKey, bool bClearSelection)
{
	if (UTypedElementSelectionSet* SelectionSet = GetEditorSelectionSet())
	{
		StoredSelectionSets.Emplace(SelectionStoreKey, SelectionSet->GetCurrentSelectionState());

		if (bClearSelection)
		{
			SelectionSet->ClearSelection(FTypedElementSelectionOptions().SetAllowHidden(true));
		}
	}
}

void FEditorModeTools::RestoreSelection(FName SelectionStoreKey)
{
	if (UTypedElementSelectionSet* SelectionSet = GetEditorSelectionSet())
	{
		if (FTypedElementSelectionSetState* StoredState = StoredSelectionSets.Find(SelectionStoreKey))
		{
			SelectionSet->RestoreSelectionState(*StoredState);
		}
	}
}

UWorld* FEditorModeTools::GetWorld() const
{
	// When in 'Simulate' mode, the editor mode tools will actually interact with the PIE world
	if( GEditor->bIsSimulatingInEditor )
	{
		return GEditor->GetPIEWorldContext()->World();
	}
	else
	{
		return GEditor->GetEditorWorldContext().World();
	}
}

FEditorViewportClient* FEditorModeTools::GetHoveredViewportClient() const
{
	// This is our best effort right now. However this is somewhat incorrect as if you Hover
	// on other Viewports they get mouse events, but this value stays on the Focused viewport.
	// Not sure what to do about this right now.
	return HoveredViewportClient;
}

FEditorViewportClient* FEditorModeTools::GetFocusedViewportClient() const
{
	// This is our best effort right now. However this is somewhat incorrect as if you Hover
	// on other Viewports they get mouse events, but this value stays on the Focused viewport.
	// Not sure what to do about this right now.
	return FocusedViewportClient;
}

bool FEditorModeTools::SelectionHasSceneComponent() const
{
	return bSelectionHasSceneComponent;
}

bool FEditorModeTools::IsSelectionAllowed(AActor* InActor, const bool bInSelected) const
{
	bool bSelectionAllowed = (ActiveScriptableModes.Num() == 0);

	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bSelectionAllowed |= Mode->IsSelectionAllowed(InActor, bInSelected);
	}

	return bSelectionAllowed;
}

bool FEditorModeTools::IsSelectionHandled(AActor* InActor, const bool bInSelected) const
{
	bool bSelectionHandled = false;
	ForEachEdMode([&bSelectionHandled, bInSelected, InActor](UEdMode* Mode)
		{
			bSelectionHandled |= Mode->Select(InActor, bInSelected);
			return true;
		});

	return bSelectionHandled;
}

bool FEditorModeTools::ProcessEditDuplicate()
{
	bool bHandled = false;
	ForEachEdMode([&bHandled](UEdMode* Mode)
		{
			bHandled |= Mode->ProcessEditDuplicate();
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::ProcessEditDelete()
{
	bool bHandled = InteractiveToolsContext->ProcessEditDelete();
	ForEachEdMode([&bHandled](UEdMode* Mode)
		{
			bHandled |= Mode->ProcessEditDelete();
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::ProcessEditCut()
{
	bool bHandled = false;
	ForEachEdMode([&bHandled](UEdMode* Mode)
		{
			bHandled = Mode->ProcessEditCut();
			return !bHandled;
		});

	return bHandled;
}

bool FEditorModeTools::ProcessEditCopy()
{
	bool bHandled = false;
	ForEachEdMode([&bHandled](UEdMode* Mode)
		{
			bHandled = Mode->ProcessEditCopy();
			return !bHandled;
		});

	return bHandled;
}

bool FEditorModeTools::ProcessEditPaste()
{
	bool bHandled = false;
	ForEachEdMode([&bHandled](UEdMode* Mode)
		{
			bHandled = Mode->ProcessEditPaste();
			return !bHandled;
		});

	return bHandled;
}

EEditAction::Type FEditorModeTools::GetActionEditDuplicate()
{
	EEditAction::Type ReturnedAction = EEditAction::Skip;
	ForEachEdMode([&ReturnedAction](UEdMode* Mode)
		{
			const EEditAction::Type EditAction = Mode->GetActionEditDuplicate();
			if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
			{
				ReturnedAction = EditAction;
				return false;
			}

			return true;
		});

	return ReturnedAction;
}

EEditAction::Type FEditorModeTools::GetActionEditDelete()
{
	EEditAction::Type ReturnedAction = EEditAction::Skip;
	ForEachEdMode([&ReturnedAction](UEdMode* Mode)
	{
		const EEditAction::Type EditAction = Mode->GetActionEditDelete();
		if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
		{
			ReturnedAction = EditAction;
			return false;
		}

		return true;
	});

	return ReturnedAction;
}

EEditAction::Type FEditorModeTools::GetActionEditCut()
{
	EEditAction::Type ReturnedAction = EEditAction::Skip;
	ForEachEdMode([&ReturnedAction](UEdMode* Mode)
		{
			const EEditAction::Type EditAction = Mode->GetActionEditCut();
			if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
			{
				ReturnedAction = EditAction;
				return false;
			}

			return true;
		});

	return ReturnedAction;
}

EEditAction::Type FEditorModeTools::GetActionEditCopy()
{
	EEditAction::Type ReturnedAction = EEditAction::Skip;
	ForEachEdMode([&ReturnedAction](UEdMode* Mode)
		{
			const EEditAction::Type EditAction = Mode->GetActionEditCopy();
			if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
			{
				ReturnedAction = EditAction;
				return false;
			}

			return true;
		});

	return ReturnedAction;
}

EEditAction::Type FEditorModeTools::GetActionEditPaste()
{
	EEditAction::Type ReturnedAction = EEditAction::Skip;
	ForEachEdMode([&ReturnedAction](UEdMode* Mode)
		{
			const EEditAction::Type EditAction = Mode->GetActionEditPaste();
			if (EditAction == EEditAction::Process || EditAction == EEditAction::Halt)
			{
				ReturnedAction = EditAction;
				return false;
			}

			return true;
		});

	return ReturnedAction;
}

void FEditorModeTools::DeactivateOtherVisibleModes(FEditorModeID InMode)
{
	TArray<UEdMode*> TempModes(ActiveScriptableModes);
	for (const UEdMode* Mode : TempModes)
	{
		if (Mode->GetID() != InMode && Mode->GetModeInfo().IsVisible())
		{
			DeactivateMode(Mode->GetID());
		}
	}
}

bool FEditorModeTools::IsSnapRotationEnabled() const
{
	bool bRetVal = false;
	ForEachEdMode([&bRetVal](UEdMode* Mode)
		{
			bRetVal = Mode->IsSnapRotationEnabled();
			return !bRetVal;
		});

	return bRetVal;
}

bool FEditorModeTools::SnapRotatorToGridOverride(FRotator& InRotation) const
{
	bool bRetVal = false;
	ForEachEdMode([&bRetVal, &InRotation](UEdMode* Mode)
		{
			bRetVal = Mode->SnapRotatorToGridOverride(InRotation);
			return !bRetVal;
		});

	return bRetVal;
}

void FEditorModeTools::ActorsDuplicatedNotify(TArray<AActor*>& InPreDuplicateSelection, TArray<AActor*>& InPostDuplicateSelection, const bool bOffsetLocations)
{
	ForEachEdMode([&InPreDuplicateSelection, &InPostDuplicateSelection, bOffsetLocations](UEdMode* Mode)
	{
		// Tell the tools about the duplication
		Mode->ActorsDuplicatedNotify(InPreDuplicateSelection, InPostDuplicateSelection, bOffsetLocations);
		return true;
	});
}

void FEditorModeTools::ActorMoveNotify()
{
	ForEachEdMode([](UEdMode* Mode)
	{
		// Also notify the current editing modes if they are interested.
		Mode->ActorMoveNotify();
		return true;
	});
}

void FEditorModeTools::ActorSelectionChangeNotify()
{
	ForEachEdMode([](UEdMode* Mode)
	{
		Mode->ActorSelectionChangeNotify();
		return true;
	});
}

void FEditorModeTools::ActorPropChangeNotify()
{
	ForEachEdMode([](UEdMode* Mode)
	{
		Mode->ActorPropChangeNotify();
		return true;
	});
}

void FEditorModeTools::UpdateInternalData()
{
	ForEachEdMode([](UEdMode* Mode)
	{
		Mode->UpdateInternalData();
		return true;
	});
}

bool FEditorModeTools::IsOnlyVisibleActiveMode(FEditorModeID InMode) const
{
	// Only return true if this is the *only* active mode
	bool bFoundAnotherVisibleMode = false;
	ForEachEdMode([&bFoundAnotherVisibleMode, InMode](UEdMode* Mode)
	{
		bFoundAnotherVisibleMode = (Mode->GetModeInfo().IsVisible() && Mode->GetID() != InMode);
		return !bFoundAnotherVisibleMode;
	});
	return !bFoundAnotherVisibleMode;
}

void FEditorModeTools::OnEditorSelectionChanged(UObject* NewSelection)
{
	if (NewSelection == GetSelectedActors())
	{
		// when actors are selected check if there is at least one component selected and cache that off
		// Editor modes use this primarily to determine of transform gizmos should be drawn.  
		// Performing this check each frame with lots of actors is expensive so only do this when selection changes
		bSelectionHasSceneComponent = false;
		for(FSelectionIterator It(*GetSelectedActors()); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			if(Actor != nullptr && Actor->FindComponentByClass<USceneComponent>() != nullptr)
			{
				bSelectionHasSceneComponent = true;
				break;
			}
		}

	}
	else
	{
		// If selecting an actor, move the pivot location.
		AActor* Actor = Cast<AActor>(NewSelection);
		if(Actor != nullptr)
		{
			if(Actor->IsSelected())
			{
				SetPivotLocation(Actor->GetActorLocation(), false);

				// If this actor wasn't part of the original selection set during pie/sie, clear it now
				if(GEditor->ActorsThatWereSelected.Num() > 0)
				{
					AActor* EditorActor = EditorUtilities::GetEditorWorldCounterpartActor(Actor);
					if(!EditorActor || !GEditor->ActorsThatWereSelected.Contains(EditorActor))
					{
						GEditor->ActorsThatWereSelected.Empty();
					}
				}
			}
			else if(GEditor->ActorsThatWereSelected.Num() > 0)
			{
				// Clear the selection set
				GEditor->ActorsThatWereSelected.Empty();
			}
		}
	}

	for(const auto& Pair : FEditorModeRegistry::Get().GetFactoryMap())
	{
		Pair.Value->OnSelectionChanged(*this, NewSelection);
	}
}

void FEditorModeTools::OnEditorSelectNone()
{
	GEditor->SelectNone( false, true );
	GEditor->ActorsThatWereSelected.Empty();
}

void FEditorModeTools::DrawBrackets(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (!ViewportClient->IsPerspective() || !GetDefault<ULevelEditorViewportSettings>()->bHighlightWithBrackets)
	{
		return;
	}

	if (UTypedElementSelectionSet* CurrentSelection = GetEditorSelectionSet())
	{
		CurrentSelection->ForEachSelectedObject<AActor>([Canvas, View, Viewport, ViewportClient](AActor* Actor)
			{
				const FLinearColor SelectedActorBoxColor(0.6f, 0.6f, 1.0f);
				const bool bDrawBracket = Actor->IsA<AStaticMeshActor>();
				ViewportClient->DrawActorScreenSpaceBoundingBox(Canvas, View, Viewport, Actor, SelectedActorBoxColor, bDrawBracket);
				return true;
			});
	}
}

void FEditorModeTools::ForEachEdMode(TFunctionRef<bool(UEdMode*)> InCalllback) const
{
	for (UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode && !Mode->IsPendingDeletion())
		{
			if (!InCalllback(Mode))
			{
				break;
			}
		}
	}
}

void FEditorModeTools::DeactivateAllModesPendingDeletion()
{
	// Reverse iterate since we are modifying the active modes list.
	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		if (ActiveScriptableModes[Index]->IsPendingDeletion())
		{
			DeactivateScriptableModeAtIndex(Index);
		}
	}
}

void FEditorModeTools::SetPivotLocation( const FVector& Location, const bool bIncGridBase )
{
	CachedLocation = PivotLocation = SnappedLocation = Location;
	if ( bIncGridBase )
	{
		GridBase = Location;
	}
}

ECoordSystem FEditorModeTools::GetCoordSystem(bool bGetRawValue)
{
	if (!bGetRawValue && (GetWidgetMode() == UE::Widget::WM_Scale))
	{
		return COORD_Local;
	}
	else
	{
		return CoordSystem;
	}
}

void FEditorModeTools::SetCoordSystem(ECoordSystem NewCoordSystem)
{
	CoordSystem = NewCoordSystem;
	BroadcastCoordSystemChanged(NewCoordSystem);
}

void FEditorModeTools::SetDefaultMode( const FEditorModeID DefaultModeID )
{
	DefaultModeIDs.Reset();
	DefaultModeIDs.Add( DefaultModeID );
}

void FEditorModeTools::AddDefaultMode( const FEditorModeID DefaultModeID )
{
	DefaultModeIDs.AddUnique( DefaultModeID );
}

void FEditorModeTools::RemoveDefaultMode( const FEditorModeID DefaultModeID )
{
	DefaultModeIDs.RemoveSingle( DefaultModeID );
}

void FEditorModeTools::ActivateDefaultMode()
{
	// NOTE: Activating EM_Default will cause ALL default editor modes to be activated (handled specially in ActivateMode())
	ActivateMode( FBuiltinEditorModes::EM_Default );
}

void FEditorModeTools::DeactivateScriptableModeAtIndex(int32 InIndex)
{
	check(InIndex >= 0 && InIndex < ActiveScriptableModes.Num());

	UEdMode* Mode = ActiveScriptableModes[InIndex];
	ActiveScriptableModes.RemoveAt(InIndex);

	// Remove the toolbar widget to remove any toolkit references that may get removed when the mode exits.
	ActiveToolBarRows.RemoveAll(
		[&Mode](FEdModeToolbarRow& Row)
		{
			return Row.ModeID == Mode->GetID();
		}
	);

	RebuildModeToolBar();

	Mode->Exit();

	const bool bIsEnteringMode = false;
	BroadcastEditorModeIDChanged(Mode->GetID(), bIsEnteringMode);

	RecycledScriptableModes.Add(Mode->GetID(), Mode);
}

void FEditorModeTools::OnModeUnregistered(FEditorModeID ModeID)
{
	DestroyMode(ModeID);
}

void FEditorModeTools::RebuildModeToolBar()
{
	// If the tab or box is not valid the toolbar has not been opened or has been closed by the user
	TSharedPtr<SVerticalBox> ModeToolbarBoxPinned = ModeToolbarBox.Pin();
	if (ModeToolbarTab.IsValid() && ModeToolbarBoxPinned.IsValid())
	{
		ModeToolbarBoxPinned->ClearChildren();

		bool bExclusivePalettes = true;
		TSharedRef<SVerticalBox> ToolBoxVBox = SNew(SVerticalBox);

		TSharedRef< SUniformWrapPanel> PaletteTabBox = SNew(SUniformWrapPanel)
			.SlotPadding(FMargin(1.f, 2.f))
			.HAlign(HAlign_Left);
		TSharedRef< SWidgetSwitcher > PaletteSwitcher = SNew(SWidgetSwitcher);

		int32 PaletteCount = ActiveToolBarRows.Num();
		if(PaletteCount > 0)
		{
			for(int32 RowIdx = 0; RowIdx < PaletteCount; ++RowIdx)
			{
				const FEdModeToolbarRow& Row = ActiveToolBarRows[RowIdx];
				if (ensure(Row.ToolbarWidget.IsValid()))
				{
					TSharedRef<SWidget> PaletteWidget = Row.ToolbarWidget.ToSharedRef();
					
					TSharedPtr<FModeToolkit> RowToolkit;
					if (FEdMode* Mode = GetActiveMode(Row.ModeID))
					{
						RowToolkit = Mode->GetToolkit();
					}
					else if (UEdMode* ScriptableMode = GetActiveScriptableMode(Row.ModeID))
					{
						RowToolkit = ScriptableMode->GetToolkit().Pin();
					}

					bExclusivePalettes = RowToolkit->HasExclusiveToolPalettes();

					if (!bExclusivePalettes)
					{
						ToolBoxVBox->AddSlot()
						.AutoHeight()
						.Padding(FMargin(2.0, 2.0))
						[
							SNew(SExpandableArea)
							.AreaTitle(Row.DisplayName)
							.AreaTitleFont(FAppStyle::Get().GetFontStyle("NormalFont"))
							.BorderImage(FAppStyle::Get().GetBrush("PaletteToolbar.ExpandableAreaHeader"))
							.BodyBorderImage(FAppStyle::Get().GetBrush("PaletteToolbar.ExpandableAreaBody"))
							.HeaderPadding(FMargin(4.f))
							.Padding(FMargin(4.0, 0.0))
							.BodyContent()
							[
								PaletteWidget	
							]
						];
					}
					else 
					{
						// Don't show Palette Tabs if there is only one
						if (PaletteCount > 1)
						{
							PaletteTabBox->AddSlot()
							[
								SNew(SCheckBox)
								.Style( FEditorStyle::Get(),  "ToolPalette.DockingTab" )
								.OnCheckStateChanged_Lambda( [PaletteSwitcher, Row, RowToolkit] (const ECheckBoxState) { 
										PaletteSwitcher->SetActiveWidget(Row.ToolbarWidget.ToSharedRef());
										RowToolkit->SetCurrentPalette(Row.PaletteName);
									} 
								)
								.IsChecked_Lambda( [PaletteSwitcher, PaletteWidget] () -> ECheckBoxState { return PaletteSwitcher->GetActiveWidget() == PaletteWidget ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
								[
									SNew(STextBlock)
									.Text(Row.DisplayName)
								]
							];
						}

						PaletteSwitcher->AddSlot()
						[
							PaletteWidget
						]; 
					}
				}
			}

			ModeToolbarBoxPinned->AddSlot()
			.AutoHeight()
			[
				SNew(SOverlay)
				+SOverlay::Slot()
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("ToolPalette.DockingWell"))
				]

				+SOverlay::Slot()
				[
					PaletteTabBox
				]
			];

			ModeToolbarBoxPinned->AddSlot()
			.AutoHeight()
			.Padding(1.f)
			[
				SNew(SBox)
				.HeightOverride(PaletteSwitcher->GetNumWidgets() > 0 ? 45.f : 0.f)
				[
					PaletteSwitcher 
				]
			];

			ModeToolbarBoxPinned->AddSlot()
			[
				SNew(SScrollBox)

				+SScrollBox::Slot()
				[
					ToolBoxVBox	
				]
			];

			ModeToolbarPaletteSwitcher = PaletteSwitcher;
		}
		else
		{
			ModeToolbarTab.Pin()->RequestCloseTab();
		}
	}
}

void FEditorModeTools::SpawnOrUpdateModeToolbar()
{
	if (ShouldShowModeToolbar())
	{
		if (ModeToolbarTab.IsValid())
		{
			RebuildModeToolBar();
		}
		else if (ToolkitHost.IsValid())
		{
			ToolkitHost.Pin()->GetTabManager()->TryInvokeTab(EditorModeToolbarTabName);
		}
	}
}

void FEditorModeTools::InvokeToolPaletteTab(FEditorModeID InModeID, FName InPaletteName)
{
	if (!ModeToolbarPaletteSwitcher.Pin()) 
	{
		return;
	}

	for (auto Row: ActiveToolBarRows)
	{
		if (Row.ModeID == InModeID && Row.PaletteName == InPaletteName)
		{
			TSharedRef<SWidget> PaletteWidget = Row.ToolbarWidget.ToSharedRef();

			TSharedPtr<FModeToolkit> RowToolkit;
			if (FEdMode* Mode = GetActiveMode(InModeID))
			{
				RowToolkit = Mode->GetToolkit();
			}
			else if (UEdMode* ScriptableMode = GetActiveScriptableMode(InModeID))
			{
				RowToolkit = ScriptableMode->GetToolkit().Pin();
			}

			TSharedPtr<SWidget> ActiveWidget = ModeToolbarPaletteSwitcher.Pin()->GetActiveWidget();
			if (RowToolkit && ActiveWidget.Get() != Row.ToolbarWidget.Get())
			{
				ModeToolbarPaletteSwitcher.Pin()->SetActiveWidget(Row.ToolbarWidget.ToSharedRef());
				RowToolkit->OnToolPaletteChanged(Row.PaletteName);
			}
			break;	
		}
	}	
}

void FEditorModeTools::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
	UWorld* World = GetWorld();
	if (InWorld == World)
	{
		DeactivateAllModesPendingDeletion();
	}
}

void FEditorModeTools::RemoveAllDelegateHandlers()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModeUnregistered().RemoveAll(this);
	}

	FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	
	// For now, check that UObjects are even valid, because the level editor has a global static mode tools
	if (UObjectInitialized())
	{
		USelection::SelectionChangedEvent.RemoveAll(this);
		USelection::SelectNoneEvent.RemoveAll(this);
		USelection::SelectObjectEvent.RemoveAll(this);
	}

	OnEditorModeIDChanged().Clear();
	OnWidgetModeChanged().Clear();
	OnCoordSystemChanged().Clear();
}

void FEditorModeTools::DeactivateMode( FEditorModeID InID )
{
	// Find the mode from the ID and exit it.
	ForEachEdMode([InID](UEdMode* Mode)
	{
		if (Mode->GetID() == InID)
		{
			Mode->RequestDeletion();
			return false;
		}

		return true;
	});
}

void FEditorModeTools::DeactivateAllModes()
{
	ForEachEdMode([](UEdMode* Mode)
	{
		Mode->RequestDeletion();
		return true;
	});
}

void FEditorModeTools::DestroyMode( FEditorModeID InID )
{
	// Since deactivating the last active mode will cause the default modes to be activated, make sure this mode is removed from defaults.
	RemoveDefaultMode( InID );
	
	// Add back the default default mode if we just removed the last valid default.
	if ( DefaultModeIDs.Num() == 0 )
	{
		AddDefaultMode( FBuiltinEditorModes::EM_Default );
	}

	// Find the mode from the ID and exit it.
	for (int32 Index = ActiveScriptableModes.Num() - 1; Index >= 0; --Index)
	{
		auto& Mode = ActiveScriptableModes[Index];
		if (Mode->GetID() == InID)
		{
			// Deactivate and destroy
			DeactivateScriptableModeAtIndex(Index);
			break;
		}
	}

	RecycledScriptableModes.Remove(InID);
}

TSharedRef<SDockTab> FEditorModeTools::MakeModeToolbarTab()
{
	TSharedRef<SDockTab> ToolbarTabRef = 	
		SNew(SDockTab)
		.Label(NSLOCTEXT("EditorModes", "EditorModesToolbarTitle", "Mode Toolbar"))
		.ContentPadding(0.0f)
		[
			SAssignNew(ModeToolbarBox, SVerticalBox)	
		];

	ModeToolbarTab = ToolbarTabRef;

	// Rebuild the toolbar with existing mode tools that may be active
	RebuildModeToolBar();

	return ToolbarTabRef;

}

bool FEditorModeTools::ShouldShowModeToolbar() const
{
	return ActiveToolBarRows.Num() > 0;
}

bool FEditorModeTools::ShouldShowModeToolbox() const
{
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		if (Mode->GetModeInfo().IsVisible() && Mode->UsesToolkits())
		{
			return true;
		}
	}

	return false;
}

void FEditorModeTools::ActivateMode(FEditorModeID InID, bool bToggle)
{
	static bool bReentrant = false;
	if( !bReentrant )
	{
		if (InID == FBuiltinEditorModes::EM_Default)
		{
			bReentrant = true;

			for( const FEditorModeID& ModeID : DefaultModeIDs )
			{
				ActivateMode( ModeID );
			}

			for( const FEditorModeID& ModeID : DefaultModeIDs )
			{
				check( IsModeActive( ModeID ) );
			}

			bReentrant = false;
			return;
		}
	}

	// Check to see if the mode is already active
	if (IsModeActive(InID))
	{
		// The mode is already active toggle it off if we should toggle off already active modes.
		if (bToggle)
		{
			DeactivateMode(InID);
		}
		// Nothing more to do
		return;
	}

	// Recycle a mode or factory a new one
	UEdMode* ScriptableMode = RecycledScriptableModes.FindRef(InID);
	if (!ScriptableMode)
	{
		ScriptableMode = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CreateEditorModeWithToolsOwner(InID, *this);
	}

	if (!ScriptableMode)
	{
		UE_LOG(LogEditorModes, Log, TEXT("FEditorModeTools::ActivateMode : Couldn't find mode '%s'."), *InID.ToString());
		// Just return and leave the mode list unmodified
		return;
	}

	// Remove anything that isn't compatible with this mode
	const bool bIsVisibleMode = ScriptableMode->GetModeInfo().IsVisible();
	for (int32 ModeIndex = ActiveScriptableModes.Num() - 1; ModeIndex >= 0; ModeIndex--)
	{
		const bool bModesAreCompatible = ScriptableMode->IsCompatibleWith(ActiveScriptableModes[ModeIndex]->GetID()) || ActiveScriptableModes[ModeIndex]->IsCompatibleWith(ScriptableMode->GetID());
		if (!bModesAreCompatible || (bIsVisibleMode && ActiveScriptableModes[ModeIndex]->GetModeInfo().IsVisible()))
		{
			ActiveScriptableModes[ModeIndex]->RequestDeletion();
		}
	}

	ActiveScriptableModes.Add(ScriptableMode);
	// Enter the new mode
	ScriptableMode->Enter();

	const bool bIsEnteringMode = true;
	BroadcastEditorModeIDChanged(ScriptableMode->GetID(), bIsEnteringMode);

	// Ask the mode to build the toolbar.
	TSharedPtr<FUICommandList> CommandList;
	const TSharedPtr<FModeToolkit> Toolkit = ScriptableMode->GetToolkit().Pin();
	if (Toolkit && !Toolkit->HasIntegratedToolPalettes())
	{
		CommandList = Toolkit->GetToolkitCommands();

		// Also build the toolkit here 
		TArray<FName> PaletteNames;
		Toolkit->GetToolPaletteNames(PaletteNames);
		for (auto Palette : PaletteNames)
		{
			FUniformToolBarBuilder ModeToolbarBuilder(CommandList, FMultiBoxCustomization(ScriptableMode->GetModeInfo().ToolbarCustomizationName), TSharedPtr<FExtender>(), false);
			ModeToolbarBuilder.SetStyle(&FEditorStyle::Get(), "PaletteToolBar");
			Toolkit->BuildToolPalette(Palette, ModeToolbarBuilder);
			ActiveToolBarRows.Emplace(ScriptableMode->GetID(), Palette, Toolkit->GetToolPaletteDisplayName(Palette), ModeToolbarBuilder.MakeWidget());
		}
	}

	SpawnOrUpdateModeToolbar();

	RecycledScriptableModes.Remove(InID);

	// Update the editor UI
	FEditorSupportDelegates::UpdateUI.Broadcast();
}

bool FEditorModeTools::EnsureNotInMode(FEditorModeID ModeID, const FText& ErrorMsg, bool bNotifyUser) const
{
	// We're in a 'safe' mode if we're not in the specified mode.
	const bool bInASafeMode = !IsModeActive(ModeID);
	if( !bInASafeMode && !ErrorMsg.IsEmpty() )
	{
		// Do we want to display this as a notification or a dialog to the user
		if ( bNotifyUser )
		{
			FNotificationInfo Info( ErrorMsg );
			FSlateNotificationManager::Get().AddNotification( Info );
		}
		else
		{
			FMessageDialog::Open( EAppMsgType::Ok, ErrorMsg );
		}		
	}
	return bInASafeMode;
}

UEdMode* FEditorModeTools::GetActiveScriptableMode(FEditorModeID InID) const
{
	if (UEdMode* const* FoundMode = ActiveScriptableModes.FindByPredicate([InID](UEdMode* Mode) { return (Mode->GetID() == InID); }))
	{
		return const_cast<UEdMode*>(*FoundMode);
	}

	return nullptr;
}

UTexture2D* FEditorModeTools::GetVertexTexture() const
{
	return GEngine->DefaultBSPVertexTexture;
}

FMatrix FEditorModeTools::GetCustomDrawingCoordinateSystem()
{
	FMatrix Matrix = FMatrix::Identity;

	switch (GetCoordSystem())
	{
		case COORD_Local:
		{
			Matrix = GetLocalCoordinateSystem();
		}
		break;

		case COORD_World:
			break;

		default:
			break;
	}

	return Matrix;
}

FMatrix FEditorModeTools::GetCustomInputCoordinateSystem()
{
	return GetCustomDrawingCoordinateSystem();
}

FMatrix FEditorModeTools::GetLocalCoordinateSystem()
{
	FMatrix Matrix = FMatrix::Identity;
	// Let the current mode have a shot at setting the local coordinate system.
	// If it doesn't want to, create it by looking at the currently selected actors list.

	bool CustomCoordinateSystemProvided = false;
	ForEachEdMode<ILegacyEdModeWidgetInterface>([&Matrix, &CustomCoordinateSystemProvided](ILegacyEdModeWidgetInterface* LegacyMode)
		{
			CustomCoordinateSystemProvided = LegacyMode->GetCustomDrawingCoordinateSystem(Matrix, nullptr);
			return !CustomCoordinateSystemProvided;
		});

	if (!CustomCoordinateSystemProvided)
	{
		if (USceneComponent* SceneComponent = GetSelectedComponents()->GetBottom<USceneComponent>())
		{
			Matrix = FQuatRotationMatrix(SceneComponent->GetComponentQuat());
		}
		else
		{
			const int32 Num = GetSelectedActors()->CountSelections<AActor>();

			// Coordinate system needs to come from the last actor selected
			if (Num > 0)
			{
				Matrix = FQuatRotationMatrix(GetSelectedActors()->GetBottom<AActor>()->GetActorQuat());
			}
		}
	}

	if (!Matrix.Equals(FMatrix::Identity))
	{
		Matrix.RemoveScaling();
	}

	return Matrix;
}

/** Gets the widget axis to be drawn */
EAxisList::Type FEditorModeTools::GetWidgetAxisToDraw( UE::Widget::EWidgetMode InWidgetMode ) const
{
	EAxisList::Type OutAxis = EAxisList::All;
	for( int Index = ActiveScriptableModes.Num() - 1; Index >= 0 ; Index-- )
	{
		ILegacyEdModeWidgetInterface* Mode = Cast<ILegacyEdModeWidgetInterface>(ActiveScriptableModes[Index]);
		if ( Mode && Mode->ShouldDrawWidget() )
		{
			OutAxis = Mode->GetWidgetAxisToDraw( InWidgetMode );
			break;
		}
	}

	return OutAxis;
}

/** Mouse tracking interface.  Passes tracking messages to all active modes */
bool FEditorModeTools::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bIsTracking = true;
	CachedLocation = PivotLocation;	// Cache the pivot location

	bool bTransactionHandled = InteractiveToolsContext->StartTracking(InViewportClient, InViewport);
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bTransactionHandled, InViewportClient, InViewport](ILegacyEdModeViewportInterface* ViewportInterface)
		{
			bTransactionHandled |= ViewportInterface->StartTracking(InViewportClient, InViewport);
			return true;
		});

	return bTransactionHandled;
}

/** Mouse tracking interface.  Passes tracking messages to all active modes */
bool FEditorModeTools::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	bIsTracking = false;
	bool bTransactionHandled = InteractiveToolsContext->EndTracking(InViewportClient, InViewport);

	ForEachEdMode<ILegacyEdModeViewportInterface>([&bTransactionHandled, InViewportClient, InViewport](ILegacyEdModeViewportInterface* ViewportInterface)
		{
			bTransactionHandled |= ViewportInterface->EndTracking(InViewportClient, InViewport);
			return true;
		});

	CachedLocation = PivotLocation;	// Clear the pivot location
	
	return bTransactionHandled;
}

bool FEditorModeTools::AllowsViewportDragTool() const
{
	bool bCanUseDragTool = false;
	ForEachEdMode<const ILegacyEdModeViewportInterface>([&bCanUseDragTool](const ILegacyEdModeViewportInterface* LegacyMode)
		{
			bCanUseDragTool |= LegacyMode->AllowsViewportDragTool();
			return true;
		});

	return bCanUseDragTool;
}

/** Notifies all active modes that a map change has occured */
void FEditorModeTools::MapChangeNotify()
{
	ForEachEdMode([](UEdMode* Mode)
		{
			Mode->MapChangeNotify();
			return true;
		});
}

/** Notifies all active modes to empty their selections */
void FEditorModeTools::SelectNone()
{
	ForEachEdMode([](UEdMode* Mode)
		{
			Mode->SelectNone();
			return true;
		});
}

/** Notifies all active modes of box selection attempts */
bool FEditorModeTools::BoxSelect( FBox& InBox, bool InSelect )
{
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeSelectInterface>([&bHandled, &InBox, InSelect](ILegacyEdModeSelectInterface* LegacyMode)
		{
			bHandled |= LegacyMode->BoxSelect(InBox, InSelect);
			return true;
		});
	return bHandled;
}

/** Notifies all active modes of frustum selection attempts */
bool FEditorModeTools::FrustumSelect( const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect )
{
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeSelectInterface>([&bHandled, InFrustum, InViewportClient, InSelect](ILegacyEdModeSelectInterface* LegacyMode)
		{
			bHandled |= LegacyMode->FrustumSelect(InFrustum, InViewportClient, InSelect);
			return true;
		});
	return bHandled;
}


/** true if any active mode uses a transform widget */
bool FEditorModeTools::UsesTransformWidget() const
{
	bool bUsesTransformWidget = false;
	ForEachEdMode<const ILegacyEdModeWidgetInterface>([&bUsesTransformWidget](const ILegacyEdModeWidgetInterface* LegacyMode)
		{
			bUsesTransformWidget |= LegacyMode->UsesTransformWidget();
			return true;
		});

	return bUsesTransformWidget;
}

/** true if any active mode uses the passed in transform widget */
bool FEditorModeTools::UsesTransformWidget( UE::Widget::EWidgetMode CheckMode ) const
{
	bool bUsesTransformWidget = false;
	ForEachEdMode<const ILegacyEdModeWidgetInterface>([&bUsesTransformWidget, CheckMode](const ILegacyEdModeWidgetInterface* LegacyMode)
		{
			bUsesTransformWidget |= LegacyMode->UsesTransformWidget(CheckMode);
			return true;
		});

	return bUsesTransformWidget;
}

/** Sets the current widget axis */
void FEditorModeTools::SetCurrentWidgetAxis( EAxisList::Type NewAxis )
{
	ForEachEdMode<ILegacyEdModeWidgetInterface>([NewAxis](ILegacyEdModeWidgetInterface* LegacyMode)
		{
			LegacyMode->SetCurrentWidgetAxis(NewAxis);
			return true;
		});
}

/** Notifies all active modes of mouse click messages. */
bool FEditorModeTools::HandleClick(FEditorViewportClient* InViewportClient,  HHitProxy *HitProxy, const FViewportClick& Click )
{
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, HitProxy, Click](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->HandleClick(InViewportClient, HitProxy, Click);
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox)
{
	bool bHandled = false;
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->ComputeBoundingBoxForViewportFocus(Actor, PrimitiveComponent, InOutBox);
	}

	return bHandled;
}

/** true if the passed in brush actor should be drawn in wireframe */	
bool FEditorModeTools::ShouldDrawBrushWireframe( AActor* InActor ) const
{
	bool bShouldDraw = false;

	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bShouldDraw |= Mode->ShouldDrawBrushWireframe(InActor);
	}

	if((ActiveScriptableModes.Num() == 0))
	{
		// We can get into a state where there are no active modes at editor startup if the builder brush is created before the default mode is activated.
		// Ensure we can see the builder brush when no modes are active.
		bShouldDraw = true;
	}
	return bShouldDraw;
}

/** true if brush vertices should be drawn */
bool FEditorModeTools::ShouldDrawBrushVertices() const
{
	if(UBrushEditingSubsystem* BrushSubsystem = GEditor->GetEditorSubsystem<UBrushEditingSubsystem>())
	{
		// Currently only geometry mode being active prevents vertices from being drawn.
		return !BrushSubsystem->IsGeometryEditorModeActive();
	}

	return true;
}

/** Ticks all active modes */
void FEditorModeTools::Tick( FEditorViewportClient* ViewportClient, float DeltaTime )
{	
	// Remove anything pending destruction
	DeactivateAllModesPendingDeletion();

	if (ActiveScriptableModes.Num() == 0)
	{
		// Ensure the default mode is active if there are no active modes.
		ActivateDefaultMode();
	}

	InteractiveToolsContext->Tick(ViewportClient, DeltaTime);
	ForEachEdMode([ViewportClient, DeltaTime](UEdMode* Mode)
		{
			if (ILegacyEdModeViewportInterface* ViewportInterface = Cast<ILegacyEdModeViewportInterface>(Mode))
			{
				ViewportInterface->Tick(ViewportClient, DeltaTime);
			}

			Mode->ModeTick(DeltaTime);
			return true;
		});
}

/** Notifies all active modes of any change in mouse movement */
bool FEditorModeTools::InputDelta( FEditorViewportClient* InViewportClient,FViewport* InViewport,FVector& InDrag,FRotator& InRot,FVector& InScale )
{
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, InViewport, &InDrag, &InRot, &InScale](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
			return true;
		});
	return bHandled;
}

/** Notifies all active modes of captured mouse movement */	
bool FEditorModeTools::CapturedMouseMove( FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY )
{
	bool bHandled = InteractiveToolsContext->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, InViewport, InMouseX, InMouseY](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->CapturedMouseMove(InViewportClient, InViewport, InMouseX, InMouseY);
			return true;
		});
	return bHandled;
}

/** Notifies all active modes of all captured mouse movement */	
bool FEditorModeTools::ProcessCapturedMouseMoves( FEditorViewportClient* InViewportClient, FViewport* InViewport, const TArrayView<FIntPoint>& CapturedMouseMoves )
{
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, InViewport, &CapturedMouseMoves](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->ProcessCapturedMouseMoves(InViewportClient, InViewport, CapturedMouseMoves);
			return true;
		});
	return bHandled;
}

/** Notifies all active modes of keyboard input via a viewport client */
bool FEditorModeTools::InputKey(FEditorViewportClient* InViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event, bool bRouteToToolsContext)
{
	const bool bWasHandledByToolsContext = bRouteToToolsContext && InteractiveToolsContext->InputKey(InViewportClient, Viewport, Key, Event);
	if (bWasHandledByToolsContext && !bIsTracking && GetInteractiveToolsContext()->InputRouter->HasActiveMouseCapture())
	{
		StartTracking(InViewportClient, Viewport);
	}
	else if (bRouteToToolsContext && bIsTracking && !GetInteractiveToolsContext()->InputRouter->HasActiveMouseCapture())
	{
		EndTracking(InViewportClient, Viewport);
	}

	// If the toolkit should process the command, it should not have been handled by ITF, or be tracked elsewhere.
	const bool bPassToToolkitCommands = bRouteToToolsContext && !bWasHandledByToolsContext;
	bool bHandled = bWasHandledByToolsContext;
	ForEachEdMode([&bHandled, bPassToToolkitCommands, Event, Key, InViewportClient, Viewport](UEdMode* Mode)
	{
		// First, always give the legacy viewport interface a chance to process they key press. This is to support any of the FModeTools that may still exist.
		if (ILegacyEdModeViewportInterface* ViewportInterface = Cast<ILegacyEdModeViewportInterface>(Mode))
		{
			if (ViewportInterface->InputKey(InViewportClient, Viewport, Key, Event))
			{
				bHandled |= true;
				return true;  // Skip passing to the mode's toolkit if the legacy mode interface handled the input.
			}
		}
		
		// Next, give the toolkit commands a chance to process the key press if the tools context did not handle the key press.
		if (bPassToToolkitCommands && (Event != IE_Released) && Mode->UsesToolkits() && Mode->GetToolkit().IsValid())
		{
			bHandled |= Mode->GetToolkit().Pin()->GetToolkitCommands()->ProcessCommandBindings(Key, FSlateApplication::Get().GetModifierKeys(), (Event == EInputEvent::IE_Repeat));
			return true;
		}

		return true;
	});

	// Finally, pass input to selected actors if nothing else handled the input
	if (!bHandled)
	{
		GetEditorSelectionSet()->ForEachSelectedObject<AActor>([Key, Event](AActor* ActorPtr)
		{
			ActorPtr->EditorKeyPressed(Key, Event);
			return true;
		});
	}
	return bHandled;
}

/** Notifies all active modes of axis movement */
bool FEditorModeTools::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	bool bHandled = false;

	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->InputAxis(InViewportClient, Viewport, ControllerId, Key, Delta, DeltaTime);
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::GetPivotForOrbit( FVector& Pivot ) const
{
	bool bHandled = false;
	// Just return the first pivot point specified by a mode
	ForEachEdMode([&Pivot, &bHandled](const UEdMode* Mode)
		{
			bHandled = Mode->GetPivotForOrbit(Pivot);
			return !bHandled;
		});
		
	return bHandled;
}

bool FEditorModeTools::MouseEnter( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y )
{
	HoveredViewportClient = InViewportClient;
	bool bHandled = InteractiveToolsContext->MouseEnter(InViewportClient, Viewport, X, Y);

	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, Viewport, X, Y](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->MouseEnter(InViewportClient, Viewport, X, Y);
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::MouseLeave( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	HoveredViewportClient = nullptr;
	bool bHandled = InteractiveToolsContext->MouseLeave(InViewportClient, Viewport);

	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, Viewport](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->MouseLeave(InViewportClient, Viewport);
			return true;
		});

	return bHandled;
}

/** Notifies all active modes that the mouse has moved */
bool FEditorModeTools::MouseMove( FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 X, int32 Y )
{
	bool bHandled = InteractiveToolsContext->MouseMove(InViewportClient, Viewport, X, Y);

	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, Viewport, X, Y](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->MouseMove(InViewportClient, Viewport, X, Y);
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::ReceivedFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	FocusedViewportClient = InViewportClient;
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, Viewport](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->ReceivedFocus(InViewportClient, Viewport);
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::LostFocus( FEditorViewportClient* InViewportClient, FViewport* Viewport )
{
	// Note that we don't reset FocusedViewportClient intentionally. EdModeInteractiveToolsContext
	// only ticks its objects once for the focused viewport to avoid multi-ticking, so if we cleared
	// it here, we'd stop ticking things in the level editor when clicking out of the viewport.
	// TODO: Conceptually, we should probably clear FocusedViewportClient here, but also have a
	// LastFocusedViewportClient property that we don't clear, to use in ticking.

	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient, Viewport](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->LostFocus(InViewportClient, Viewport);
			return true;
		});

	return bHandled;
}

/** Draws all active mode components */	
void FEditorModeTools::DrawActiveModes( const FSceneView* InView, FPrimitiveDrawInterface* PDI )
{
	ForEachEdMode<ILegacyEdModeDrawHelperInterface>([InView, PDI](ILegacyEdModeDrawHelperInterface* DrawHelper)
		{
			DrawHelper->Draw(InView, PDI);
			return true;
		});
}

/** Renders all active modes */
void FEditorModeTools::Render( const FSceneView* InView, FViewport* Viewport, FPrimitiveDrawInterface* PDI )
{
	InteractiveToolsContext->Render(InView, Viewport, PDI);

	ForEachEdMode<ILegacyEdModeWidgetInterface>([InView, Viewport, PDI](ILegacyEdModeWidgetInterface* Mode)
		{
			Mode->Render(InView, Viewport, PDI);
			return true;
		});
}

/** Draws the HUD for all active modes */
void FEditorModeTools::DrawHUD( FEditorViewportClient* InViewportClient,FViewport* Viewport, const FSceneView* View, FCanvas* Canvas )
{
	InteractiveToolsContext->DrawHUD(InViewportClient, Viewport, View, Canvas);

	DrawBrackets(InViewportClient, Viewport, View, Canvas);

	if (!(InViewportClient->EngineShowFlags.ModeWidgets))
	{
		return;
	}

	// Clear Hit proxies
	const bool bIsHitTesting = Canvas->IsHitTesting();
	if (!bIsHitTesting)
	{
		Canvas->SetHitProxy(nullptr);
	}

	ForEachEdMode<ILegacyEdModeWidgetInterface>([InViewportClient, Viewport, View, Canvas](ILegacyEdModeWidgetInterface* Mode)
		{
			Mode->DrawHUD(InViewportClient, Viewport, View, Canvas);
			return true;
		});

	// Draw vertices for selected BSP brushes and static meshes if the large vertices show flag is set.
	if (!InViewportClient->bDrawVertices)
	{
		return;
	}

	const bool bLargeVertices = View->Family->EngineShowFlags.LargeVertices;
	if (!bLargeVertices)
	{
		return;
	}

	// Temporaries.
	const bool bShowBrushes = View->Family->EngineShowFlags.Brushes;
	const bool bShowBSP = View->Family->EngineShowFlags.BSP;
	const bool bShowBuilderBrush = View->Family->EngineShowFlags.BuilderBrush != 0;

	UTexture2D* VertexTexture = GetVertexTexture();
	const float TextureSizeX = VertexTexture->GetSizeX() * (bLargeVertices ? 1.0f : 0.5f);
	const float TextureSizeY = VertexTexture->GetSizeY() * (bLargeVertices ? 1.0f : 0.5f);

	GetEditorSelectionSet()->ForEachSelectedObject<AStaticMeshActor>([View, Canvas, VertexTexture, TextureSizeX, TextureSizeY, bIsHitTesting](AStaticMeshActor* Actor)
		{
			TArray<FVector> Vertices;
			FCanvasItemTestbed::bTestState = !FCanvasItemTestbed::bTestState;

			// Static mesh vertices
			if (Actor->GetStaticMeshComponent() && Actor->GetStaticMeshComponent()->GetStaticMesh()
				&& Actor->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData())
			{
				FTransform ActorToWorld = Actor->ActorToWorld();
				const FPositionVertexBuffer& VertexBuffer = Actor->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
				for (uint32 i = 0; i < VertexBuffer.GetNumVertices(); i++)
				{
					Vertices.AddUnique(ActorToWorld.TransformPosition(VertexBuffer.VertexPosition(i)));
				}

				const float InvDpiScale = 1.0f / Canvas->GetDPIScale();

				FCanvasTileItem TileItem(FVector2D(0.0f, 0.0f), FVector2D(0.0f, 0.0f), FLinearColor::White);
				TileItem.BlendMode = SE_BLEND_Translucent;
				for (int32 VertexIndex = 0; VertexIndex < Vertices.Num(); ++VertexIndex)
				{
					const FVector& Vertex = Vertices[VertexIndex];
					FVector2D PixelLocation;
					if (View->ScreenToPixel(View->WorldToScreen(Vertex), PixelLocation))
					{
						PixelLocation *= InvDpiScale;

						const bool bOutside =
							PixelLocation.X < 0.0f || PixelLocation.X > View->UnscaledViewRect.Width() * InvDpiScale ||
							PixelLocation.Y < 0.0f || PixelLocation.Y > View->UnscaledViewRect.Height() * InvDpiScale;
						if (!bOutside)
						{
							const float X = PixelLocation.X - (TextureSizeX / 2);
							const float Y = PixelLocation.Y - (TextureSizeY / 2);
							if (bIsHitTesting)
							{
								Canvas->SetHitProxy(new HStaticMeshVert(Actor, Vertex));
							}
							TileItem.Texture = VertexTexture->GetResource();

							TileItem.Size = FVector2D(TextureSizeX, TextureSizeY);
							Canvas->DrawItem(TileItem, FVector2D(X, Y));
							if (bIsHitTesting)
							{
								Canvas->SetHitProxy(nullptr);
							}
						}
					}
				}
			}

			return true;
		});
}

/** Calls PostUndo on all active modes */
void FEditorModeTools::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		ForEachEdMode([](UEdMode* Mode)
			{
				Mode->PostUndo();
				return true;
			});
	}
}
void FEditorModeTools::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

/** true if we should allow widget move */
bool FEditorModeTools::AllowWidgetMove() const
{
	bool bAllow = false;
	ForEachEdMode<ILegacyEdModeWidgetInterface>([&bAllow](ILegacyEdModeWidgetInterface* LegacyMode)
		{
			bAllow |= LegacyMode->AllowWidgetMove();
			return true;
		});

	return bAllow;
}

bool FEditorModeTools::DisallowMouseDeltaTracking() const
{
	bool bDisallow = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bDisallow](ILegacyEdModeViewportInterface* LegacyMode)
		{
			bDisallow |= LegacyMode->DisallowMouseDeltaTracking();
			return true;
		});

	return bDisallow;
}

bool FEditorModeTools::GetCursor(EMouseCursor::Type& OutCursor) const
{
	bool bHandled = false;
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->GetCursor(OutCursor);
	}
	return bHandled;
}

bool FEditorModeTools::GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const
{
	bool bHandled = false;
	for (const UEdMode* Mode : ActiveScriptableModes)
	{
		bHandled |= Mode->GetOverrideCursorVisibility(bWantsOverride, bHardwareCursorVisible, bSoftwareCursorVisible);
	}
	return bHandled;
}

bool FEditorModeTools::PreConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient](ILegacyEdModeViewportInterface* Mode)
		{
			bHandled |= Mode->PreConvertMouseMovement(InViewportClient);
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::PostConvertMouseMovement(FEditorViewportClient* InViewportClient)
{
	bool bHandled = false;
	ForEachEdMode<ILegacyEdModeViewportInterface>([&bHandled, InViewportClient](ILegacyEdModeViewportInterface* ViewportInterface)
		{
			bHandled |= ViewportInterface->PostConvertMouseMovement(InViewportClient);
			return true;
		});

	return bHandled;
}

bool FEditorModeTools::GetShowWidget() const
{
	bool bDrawModeSupportsWidgetDrawing = false;
	// Check to see of any active modes support widget drawing
	ForEachEdMode<ILegacyEdModeWidgetInterface>([&bDrawModeSupportsWidgetDrawing](ILegacyEdModeWidgetInterface* LegacyMode)
		{
			bDrawModeSupportsWidgetDrawing |= LegacyMode->ShouldDrawWidget();
			return true;
		});
	return bDrawModeSupportsWidgetDrawing && bShowWidget;
}

/**
 * Used to cycle widget modes
 */
void FEditorModeTools::CycleWidgetMode (void)
{
	//make sure we're not currently tracking mouse movement.  If we are, changing modes could cause a crash due to referencing an axis/plane that is incompatible with the widget
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (ViewportClient->IsTracking())
		{
			return;
		}
	}

	//only cycle when the mode is requesting the drawing of a widget
	if( GetShowWidget() )
	{
		const int32 CurrentWk = GetWidgetMode();
		int32 Wk = CurrentWk;
		do
		{
			Wk++;
			if ((Wk == UE::Widget::WM_TranslateRotateZ) && (!GetDefault<ULevelEditorViewportSettings>()->bAllowTranslateRotateZWidget))
			{
				Wk++;
			}
			// Roll back to the start if we go past UE::Widget::WM_Scale
			if( Wk >= UE::Widget::WM_Max)
			{
				Wk -= UE::Widget::WM_Max;
			}
		}
		while (!UsesTransformWidget((UE::Widget::EWidgetMode)Wk) && Wk != CurrentWk);
		SetWidgetMode( (UE::Widget::EWidgetMode)Wk );
		FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	}
}

/**Save Widget Settings to Ini file*/
void FEditorModeTools::SaveWidgetSettings(void)
{
	GetMutableDefault<UEditorPerProjectUserSettings>()->SaveConfig();
}

/**Load Widget Settings from Ini file*/
void FEditorModeTools::LoadWidgetSettings(void)
{
}

/**
 * Returns a good location to draw the widget at.
 */

FVector FEditorModeTools::GetWidgetLocation() const
{
	for (int Index = ActiveScriptableModes.Num() - 1; Index >= 0; Index--)
	{
		if (ILegacyEdModeWidgetInterface* LegacyMode = Cast<ILegacyEdModeWidgetInterface>(ActiveScriptableModes[Index]))
		{
			if (LegacyMode->UsesTransformWidget())
			{
				return LegacyMode->GetWidgetLocation();
			}
		}
	}
	
	return FVector(ForceInitToZero);
}

/**
 * Changes the current widget mode.
 */

void FEditorModeTools::SetWidgetMode( UE::Widget::EWidgetMode InWidgetMode )
{
	WidgetMode = InWidgetMode;
}

/**
 * Allows you to temporarily override the widget mode.  Call this function again
 * with UE::Widget::WM_None to turn off the override.
 */

void FEditorModeTools::SetWidgetModeOverride( UE::Widget::EWidgetMode InWidgetMode )
{
	OverrideWidgetMode = InWidgetMode;
}

/**
 * Retrieves the current widget mode, taking overrides into account.
 */

UE::Widget::EWidgetMode FEditorModeTools::GetWidgetMode() const
{
	if( OverrideWidgetMode != UE::Widget::WM_None )
	{
		return OverrideWidgetMode;
	}

	return WidgetMode;
}


/**
* Set Scale On The Widget
*/

void FEditorModeTools::SetWidgetScale(float InScale)
{
	WidgetScale = InScale;
}

/**
* Get Scale On The Widget
*/

float FEditorModeTools::GetWidgetScale() const
{
	return WidgetScale;
}

void FEditorModeTools::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObjects(ActiveScriptableModes);
	Collector.AddReferencedObjects(RecycledScriptableModes);
	Collector.AddReferencedObject(InteractiveToolsContext);
}

FEdMode* FEditorModeTools::GetActiveMode( FEditorModeID InID )
{
	if (UEdMode* Mode = GetActiveScriptableMode(InID))
	{
		return Mode->AsLegacyMode();
	}

	return nullptr;
}

const FEdMode* FEditorModeTools::GetActiveMode( FEditorModeID InID ) const
{
	if (UEdMode* Mode = GetActiveScriptableMode(InID))
	{
		return Mode->AsLegacyMode();
	}

	return nullptr;
}

const FModeTool* FEditorModeTools::GetActiveTool( FEditorModeID InID ) const
{
	ILegacyEdModeToolInterface* ActiveMode = Cast<ILegacyEdModeToolInterface>(GetActiveScriptableMode( InID ));
	const FModeTool* Tool = nullptr;
	if( ActiveMode )
	{
		Tool = ActiveMode->GetCurrentTool();
	}
	return Tool;
}

bool FEditorModeTools::IsModeActive( FEditorModeID InID ) const
{
	return (GetActiveScriptableMode(InID) != nullptr);
}

bool FEditorModeTools::IsDefaultModeActive() const
{
	bool bAllDefaultModesActive = true;
	for( const FEditorModeID& ModeID : DefaultModeIDs )
	{
		if( !IsModeActive( ModeID ) )
		{
			bAllDefaultModesActive = false;
			break;
		}
	}
	return bAllDefaultModesActive;
}

bool FEditorModeTools::CanCycleWidgetMode() const
{
	bool bCanCycleWidget = false;
	ForEachEdMode<ILegacyEdModeWidgetInterface>([&bCanCycleWidget](ILegacyEdModeWidgetInterface* LegacyMode)
		{
			bCanCycleWidget = LegacyMode->CanCycleWidgetMode();
			return !bCanCycleWidget;
		});
	return bCanCycleWidget;
}


bool FEditorModeTools::CanAutoSave() const
{
	bool bCanAutoSave = true;
	ForEachEdMode([&bCanAutoSave](const UEdMode* Mode)
		{
			if (!Mode->CanAutoSave())
			{
				bCanAutoSave = false;
				return false;
			}

			return true;
		});

	return bCanAutoSave;
}

UEdModeInteractiveToolsContext* FEditorModeTools::GetInteractiveToolsContext() const
{
	return InteractiveToolsContext;
}
