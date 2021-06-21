// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigEditModeTools.h"
#include "ControlRigControlsProxy.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "ISequencer.h"
#include "PropertyHandle.h"
#include "ControlRig.h"
#include "ControlRigEditModeSettings.h"
#include "IDetailRootObjectCustomization.h"
#include "Modules/ModuleManager.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "SControlHierarchy.h"
#include "SControlPicker.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Rigs/FKControlRig.h"
#include "SControlRigBaseListWidget.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SControlRigTweenWidget.h"
#include "IControlRigEditorModule.h"
#include "Framework/Docking/TabManager.h"
#include "ControlRigEditorStyle.h"

#define LOCTEXT_NAMESPACE "ControlRigRootCustomization"

class FControlRigEditModeGenericDetails : public IDetailCustomization
{
public:
	FControlRigEditModeGenericDetails() = delete;
	FControlRigEditModeGenericDetails(FEditorModeTools* InModeTools) : ModeTools(InModeTools) {}
	
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance(FEditorModeTools* InModeTools)
	{
		return MakeShareable(new FControlRigEditModeGenericDetails(InModeTools));
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailLayout) override
	{
		TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
		DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);

		TArray<UControlRigControlsProxy*> ProxiesBeingCustomized;
		for (TWeakObjectPtr<UObject> ObjectBeingCustomized : ObjectsBeingCustomized)
		{
			if (UControlRigControlsProxy* Proxy = Cast< UControlRigControlsProxy>(ObjectBeingCustomized.Get()))
			{
				ProxiesBeingCustomized.Add(Proxy);
			}
		}

		if (ProxiesBeingCustomized.Num() == 0)
		{
			return;
		}

		IDetailCategoryBuilder& Category = DetailLayout.EditCategory(TEXT("Control"), LOCTEXT("Channels", "Channels"));

		for (UControlRigControlsProxy* Proxy : ProxiesBeingCustomized)
		{
			FRigControlElement* ControlElement = Proxy->GetControlElement();
			if(ControlElement == nullptr)
			{
				continue;
			}
			
			FName ValuePropertyName = TEXT("Transform");
			if (ControlElement->Settings.ControlType == ERigControlType::Float)
			{
				ValuePropertyName = TEXT("Float");
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Integer)
			{
				if (ControlElement->Settings.ControlEnum == nullptr)
				{
					ValuePropertyName = TEXT("Integer");
				}
				else
				{
					ValuePropertyName = TEXT("Enum");
				}
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Bool)
			{
				ValuePropertyName = TEXT("Bool");
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Position ||
				ControlElement->Settings.ControlType == ERigControlType::Scale)
			{
				ValuePropertyName = TEXT("Vector");
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Vector2D)
			{
				ValuePropertyName = TEXT("Vector2D");
			}

			TSharedPtr<IPropertyHandle> ValuePropertyHandle = DetailLayout.GetProperty(ValuePropertyName, Proxy->GetClass());
			if (ValuePropertyHandle)
			{
				ValuePropertyHandle->SetPropertyDisplayName(FText::FromName(ControlElement->Settings.DisplayName));
			}

			URigHierarchy* Hierarchy = Proxy->ControlRig->GetHierarchy();
			Hierarchy->ForEach<FRigControlElement>([Hierarchy, Proxy, &Category, this](FRigControlElement* ControlElement) -> bool
            {
				FName ParentControlName = NAME_None;
				FRigControlElement* ParentControlElement = Cast<FRigControlElement>(Hierarchy->GetFirstParent(ControlElement));
				if(ParentControlElement)
				{
					ParentControlName = ParentControlElement->GetName();
				}
				
				if (ParentControlName == ControlElement->GetName())
				{
					if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
					{
						if (UObject* NestedProxy = EditMode->ControlProxy->FindProxy(ControlElement->GetName()))
						{
							FName PropertyName(NAME_None);
							switch (ControlElement->Settings.ControlType)
							{
								case ERigControlType::Bool:
								{
									PropertyName = TEXT("Bool");
									break;
								}
								case ERigControlType::Float:
								{
									PropertyName = TEXT("Float");
									break;
								}
								case ERigControlType::Integer:
								{
									if (ControlElement->Settings.ControlEnum == nullptr)
									{
										PropertyName = TEXT("Integer");
									}
									else
									{
										PropertyName = TEXT("Enum");
									}
									break;
								}
								default:
								{
									break;
								}
							}

							if (PropertyName.IsNone())
							{
								return true;
							}

							TArray<UObject*> NestedProxies;
							NestedProxies.Add(NestedProxy);

							FAddPropertyParams Params;
							Params.CreateCategoryNodes(false);

							IDetailPropertyRow* NestedRow = Category.AddExternalObjectProperty(
								NestedProxies,
								PropertyName,
								EPropertyLocation::Advanced,
								Params);
							NestedRow->DisplayName(FText::FromName(ControlElement->Settings.DisplayName));

							Category.SetShowAdvanced(true);
						}
					}
				}
				return true;
			});
		}
	}
protected:
	FEditorModeTools* ModeTools = nullptr;
};

void SControlRigEditModeTools::SetControlRig(UControlRig* ControlRig)
{
	SequencerRig = ControlRig;
	ViewportRig = ControlRig;
	if (SequencerRig.IsValid())
	{
		if (UControlRig* InteractionRig = SequencerRig->GetInteractionRig())
		{
			ViewportRig = InteractionRig;
		}
	}

	TArray<TWeakObjectPtr<>> Objects;
	Objects.Add(SequencerRig);
	RigOptionsDetailsView->SetObjects(Objects);

	ControlHierarchy->SetControlRig(ViewportRig.Get());
}

void SControlRigEditModeTools::Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode,UWorld* InWorld)
{
	// initialize settings view
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowModifiedPropertiesOption = true;
		DetailsViewArgs.bCustomNameAreaLocation = true;
		DetailsViewArgs.bCustomFilterAreaLocation = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
		DetailsViewArgs.bShowScrollBar = false; // Don't need to show this, as we are putting it in a scroll box
	}
	
	ModeTools = InEditMode.GetModeManager();

	ControlDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	ControlDetailsView->SetKeyframeHandler(SharedThis(this));
	ControlDetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateSP(this, &SControlRigEditModeTools::ShouldShowPropertyOnDetailCustomization));
	ControlDetailsView->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateSP(this, &SControlRigEditModeTools::IsReadOnlyPropertyOnDetailCustomization));
	ControlDetailsView->SetGenericLayoutDetailsDelegate(FOnGetDetailCustomizationInstance::CreateStatic(&FControlRigEditModeGenericDetails::MakeInstance, ModeTools));

	RigOptionsDetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	RigOptionsDetailsView->SetKeyframeHandler(SharedThis(this));
	RigOptionsDetailsView->OnFinishedChangingProperties().AddSP(this, &SControlRigEditModeTools::OnRigOptionFinishedChange);

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(PickerExpander, SExpandableArea)
				.InitiallyCollapsed(true)
				.AreaTitle(LOCTEXT("Picker_Header", "Controls"))
				.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
				.BodyContent()
				[
					SAssignNew(ControlHierarchy, SControlHierarchy, InEditMode.GetControlRig(true))
				]
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				ControlDetailsView.ToSharedRef()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(RigOptionExpander, SExpandableArea)
				.InitiallyCollapsed(true)
				.Visibility(this, &SControlRigEditModeTools::GetRigOptionExpanderVisibility)
				.AreaTitle(LOCTEXT("RigOption_Header", "Rig Options"))
				.AreaTitleFont(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.BorderBackgroundColor(FLinearColor(.6f, .6f, .6f))
				.BodyContent()
				[
					RigOptionsDetailsView.ToSharedRef()
				]
			]
		]
	];
}

void SControlRigEditModeTools::SetDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects)
{
	ControlDetailsView->SetObjects(InObjects);
}

void SControlRigEditModeTools::SetSequencer(TWeakPtr<ISequencer> InSequencer)
{
	WeakSequencer = InSequencer.Pin();
}

bool SControlRigEditModeTools::IsPropertyKeyable(const UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	if (InObjectClass && InObjectClass->IsChildOf(UControlRigTransformNoScaleControlProxy::StaticClass()) && InObjectClass->IsChildOf(UControlRigEulerTransformControlProxy::StaticClass()) && InPropertyHandle.GetProperty()
		&& InPropertyHandle.GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigTransformControlProxy, Transform)) 
	{
		return true;
	}
	FCanKeyPropertyParams CanKeyPropertyParams(InObjectClass, InPropertyHandle);
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer.IsValid() && Sequencer->CanKeyProperty(CanKeyPropertyParams))
	{
		return true;
	}

	return false;
}

bool SControlRigEditModeTools::IsPropertyKeyingEnabled() const
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence())
	{
		return true;
	}

	return false;
}

bool SControlRigEditModeTools::IsPropertyAnimated(const IPropertyHandle& PropertyHandle, UObject *ParentObject) const
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer.IsValid() && Sequencer->GetFocusedMovieSceneSequence())
	{
		FGuid ObjectHandle = Sequencer->GetHandleToObject(ParentObject);
		if (ObjectHandle.IsValid()) 
		{
			UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
			FProperty* Property = PropertyHandle.GetProperty();
			TSharedRef<FPropertyPath> PropertyPath = FPropertyPath::CreateEmpty();
			PropertyPath->AddProperty(FPropertyInfo(Property));
			FName PropertyName(*PropertyPath->ToString(TEXT(".")));
			TSubclassOf<UMovieSceneTrack> TrackClass; //use empty @todo find way to get the UMovieSceneTrack from the Property type.
			return MovieScene->FindTrack(TrackClass, ObjectHandle, PropertyName) != nullptr;
		}
	}
	return false;
}

void SControlRigEditModeTools::OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle)
{
	if (WeakSequencer.IsValid() && !WeakSequencer.Pin()->IsAllowedToChange())
	{
		return;
	}

	TArray<UObject*> Objects;
	KeyedPropertyHandle.GetOuterObjects(Objects);
	for (UObject *Object : Objects)
	{
		UControlRigControlsProxy* Proxy = Cast< UControlRigControlsProxy>(Object);
		if (Proxy)
	{
			Proxy->SetKey(KeyedPropertyHandle);
		}
	}
}

bool SControlRigEditModeTools::ShouldShowPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeVisible = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName) || InProperty.HasMetaData(FRigVMStruct::OutputMetaName);

	/*	// Show 'PickerIKTogglePos' properties
		bShow |= (InProperty.GetFName() == GET_MEMBER_NAME_CHECKED(FLimbControl, PickerIKTogglePos));
		bShow |= (InProperty.GetFName() == GET_MEMBER_NAME_CHECKED(FSpineControl, PickerIKTogglePos));
*/

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
		bShow |= OwnerClass == UControlRigEditModeSettings::StaticClass();
		bShow |= OwnerClass == UControlRigTransformControlProxy::StaticClass();		
		bShow |= OwnerClass == UControlRigTransformNoScaleControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEulerTransformControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigFloatControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVectorControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVector2DControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigBoolControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEnumControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigIntegerControlProxy::StaticClass();

		return bShow;
	};

	bool bContainsVisibleProperty = false;
	if (InPropertyAndParent.Property.IsA<FStructProperty>())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(&InPropertyAndParent.Property);
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			if (ShouldPropertyBeVisible(**PropertyIt))
			{
				return true;
			}
		}
	}

	return ShouldPropertyBeVisible(InPropertyAndParent.Property) || 
		(InPropertyAndParent.ParentProperties.Num() > 0 && ShouldPropertyBeVisible(*InPropertyAndParent.ParentProperties[0]));
}

bool SControlRigEditModeTools::IsReadOnlyPropertyOnDetailCustomization(const FPropertyAndParent& InPropertyAndParent) const
{
	auto ShouldPropertyBeEnabled = [](const FProperty& InProperty)
	{
		bool bShow = InProperty.HasAnyPropertyFlags(CPF_Interp) || InProperty.HasMetaData(FRigVMStruct::InputMetaName);

		// Always show settings properties
		const UClass* OwnerClass = InProperty.GetOwner<UClass>();
		bShow |= OwnerClass == UControlRigEditModeSettings::StaticClass();
		bShow |= OwnerClass == UControlRigTransformControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigTransformNoScaleControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEulerTransformControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigFloatControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVectorControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigVector2DControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigBoolControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigEnumControlProxy::StaticClass();
		bShow |= OwnerClass == UControlRigIntegerControlProxy::StaticClass();


		return bShow;
	};

	bool bContainsVisibleProperty = false;
	if (InPropertyAndParent.Property.IsA<FStructProperty>())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(&InPropertyAndParent.Property);
		for (TFieldIterator<FProperty> PropertyIt(StructProperty->Struct); PropertyIt; ++PropertyIt)
		{
			if (ShouldPropertyBeEnabled(**PropertyIt))
			{
				return false;
			}
		}
	}

	return !(ShouldPropertyBeEnabled(InPropertyAndParent.Property) || 
		(InPropertyAndParent.ParentProperties.Num() > 0 && ShouldPropertyBeEnabled(*InPropertyAndParent.ParentProperties[0])));
}

static bool bPickerChangingSelection = false;

void SControlRigEditModeTools::OnManipulatorsPicked(const TArray<FName>& Manipulators)
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		if (!bPickerChangingSelection)
		{
			TGuardValue<bool> SelectGuard(bPickerChangingSelection, true);
			ControlRigEditMode->ClearRigElementSelection((uint32)ERigElementType::Control);
			ControlRigEditMode->SetRigElementSelection(ERigElementType::Control, Manipulators, true);
		}
	}
}

void SControlRigEditModeTools::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	if (bPickerChangingSelection)
	{
		return;
	}

	TGuardValue<bool> SelectGuard(bPickerChangingSelection, true);
	switch (InNotifType)
	{
		case ERigVMGraphNotifType::NodeSelected:
		case ERigVMGraphNotifType::NodeDeselected:
		{
			URigVMNode* Node = Cast<URigVMNode>(InSubject);
			if (Node)
			{
				// those are not yet implemented yet
				// ControlPicker->SelectManipulator(Node->Name, InType == EControlRigModelNotifType::NodeSelected);
			}
			break;
		}
		default:
		{
			break;
		}
	}
}

EVisibility SControlRigEditModeTools::GetRigOptionExpanderVisibility() const
{
	if (UControlRig* ControlRig = SequencerRig.Get())
	{
		if (Cast<UFKControlRig>(ControlRig))
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Hidden;
}

void SControlRigEditModeTools::OnRigOptionFinishedChange(const FPropertyChangedEvent& PropertyChangedEvent)
{
	SetControlRig(SequencerRig.Get());

	if (FControlRigEditMode* EditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName)))
	{
		EditMode->SetObjects_Internal();
	}
}

void SControlRigEditModeTools::CustomizeToolBarPalette(FToolBarBuilder& ToolBarBuilder)
{
	//TOGGLE SELECTED RIG CONTROLS
	ToolBarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateLambda([this] {
		FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
		if (ControlRigEditMode)
		{
			ControlRigEditMode->SetOnlySelectRigControls(!ControlRigEditMode->GetOnlySelectRigControls());
		}
	}),
			FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this] {
		FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
		if (ControlRigEditMode)
		{
			return ControlRigEditMode->GetOnlySelectRigControls();
		}
		return false;
	})
		),
		NAME_None,
		LOCTEXT("OnlySelectControls", "Select"),
		LOCTEXT("OnlySelectControlsTooltip", "Only Select Control Rig Controls"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.OnlySelectControls")),
		EUserInterfaceActionType::ToggleButton
		);
	ToolBarBuilder.AddSeparator();

	//POSES
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakePoseDialog),
		NAME_None,
		LOCTEXT("Poses", "Poses"),
		LOCTEXT("PosesTooltip", "Show Poses"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.PoseTool")),
		EUserInterfaceActionType::Button
	);
	ToolBarBuilder.AddSeparator();

	// Tweens
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakeTweenDialog),
		NAME_None,
		LOCTEXT("Tweens", "Tweens"),
		LOCTEXT("TweensTooltip", "Create Tweens"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.TweenTool")),
		EUserInterfaceActionType::Button
	);

	// Snap
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakeSnapperDialog),
		NAME_None,
		LOCTEXT("Snapper", "Snapper"),
		LOCTEXT("SnapperTooltip", "Snap child objects to a parent object over a set of frames"),
		FSlateIcon(TEXT("ControlRigEditorStyle"), TEXT("ControlRig.SnapperTool")),
		EUserInterfaceActionType::Button
	);
	/*
	// Pivot
	ToolBarBuilder.AddToolBarButton(
		FExecuteAction::CreateSP(this, &SControlRigEditModeTools::MakeTempPivotDialog),
		NAME_None,
		LOCTEXT("TempPivot", "Temp Pivot"),
		LOCTEXT("TempPivotTooltip", "Create a temporary pivot to transform the selected Control"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "AnimationEditor.ApplyAnimation"), //MZ todo replace with correct icon
		EUserInterfaceActionType::Button
	);
	*/
	ToolBarBuilder.AddSeparator();
}

void SControlRigEditModeTools::MakePoseDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigPoseTab);
	}
}

void SControlRigEditModeTools::MakeTweenDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigTweenTab);
	}
}


void SControlRigEditModeTools::MakeSnapperDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigSnapperTab);
	}
}



void SControlRigEditModeTools::MakeTempPivotDialog()
{
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(ModeTools->GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(IControlRigEditorModule::ControlRigTempPivotTab);
	}
}


/* MZ TODO
void SControlRigEditModeTools::MakeSelectionSetDialog()
{

	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode)
	{
		TSharedPtr<SWindow> ExistingWindow = SelectionSetWindow.Pin();
		if (ExistingWindow.IsValid())
		{
			ExistingWindow->BringToFront();
		}
		else
		{
			ExistingWindow = SNew(SWindow)
				.Title(LOCTEXT("SelectionSets", "Selection Set"))
				.HasCloseButton(true)
				.SupportsMaximize(false)
				.SupportsMinimize(false)
				.ClientSize(FVector2D(165, 200));
			TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
			if (RootWindow.IsValid())
			{
				FSlateApplication::Get().AddWindowAsNativeChild(ExistingWindow.ToSharedRef(), RootWindow.ToSharedRef());
			}
			else
			{
				FSlateApplication::Get().AddWindow(ExistingWindow.ToSharedRef());
			}

		}

		ExistingWindow->SetContent(
			SNew(SControlRigBaseListWidget)
		);
		SelectionSetWindow = ExistingWindow;
	}
}
*/
FText SControlRigEditModeTools::GetActiveToolName() const
{
	return  FText();
}

FText SControlRigEditModeTools::GetActiveToolMessage() const
{
	return  FText();
}

#undef LOCTEXT_NAMESPACE
