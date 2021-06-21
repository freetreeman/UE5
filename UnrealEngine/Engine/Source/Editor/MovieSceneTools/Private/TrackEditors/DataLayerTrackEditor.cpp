// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackEditors/DataLayerTrackEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/StyleColors.h"
#include "EditorStyleSet.h"
#include "Sections/MovieSceneDataLayerSection.h"
#include "Tracks/MovieSceneDataLayerTrack.h"
#include "SequencerUtilities.h"
#include "LevelUtils.h"
#include "MovieSceneTimeHelpers.h"
#include "ISequencerSection.h"
#include "SequencerSectionPainter.h"
#include "DataLayer/DataLayerEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "DataLayerTrackEditor"

struct FDataLayerSection
	: public ISequencerSection
	, public TSharedFromThis<FDataLayerSection>
{
	FDataLayerSection(UMovieSceneDataLayerSection* InSection)
		: WeakSection(InSection)
	{}

	/*~ ISequencerSection */
	virtual UMovieSceneSection* GetSectionObject() override
	{
		return WeakSection.Get();
	}
	virtual int32 OnPaintSection(FSequencerSectionPainter& InPainter) const override
	{
		return InPainter.PaintSectionBackground();
	}
	virtual float GetSectionHeight() const override
	{
		return 30.f;
	}
	virtual TSharedRef<SWidget> GenerateSectionWidget() override
	{
		return SNew(SBox)
		.Padding(FMargin(4.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(this, &FDataLayerSection::GetVisibilityText)
				.ColorAndOpacity(this, &FDataLayerSection::GetTextColor)
				.TextStyle(FAppStyle::Get(), "NormalText.Important")
			]

			+ SVerticalBox::Slot()
			[
				SNew(STextBlock)
				.Text(this, &FDataLayerSection::GetLayerBarText)
				.AutoWrapText(true)
			]
		];
	}

	FText GetVisibilityText() const
	{
		UMovieSceneDataLayerSection* Section = WeakSection.Get();
		if (Section)
		{
			switch (Section->GetDesiredState())
			{
			case EDataLayerState::Unloaded:  return LOCTEXT("VisibilityText_Unloaded", "Unload");
			case EDataLayerState::Loaded:    return LOCTEXT("VisibilityText_Loaded", "Load");
			default: break;
			}
		}

		return LOCTEXT("VisibilityText_Activated", "Activate");
	}

	FText GetLayerBarText() const
	{
		UMovieSceneDataLayerSection* Section   = WeakSection.Get();
		UDataLayerEditorSubsystem*   SubSystem = UDataLayerEditorSubsystem::Get();

		if (SubSystem && Section)
		{
			FString LayerName;

			const TArray<FActorDataLayer>& DataLayers = Section->GetDataLayers();
			for (int32 Index = 0; Index < DataLayers.Num(); ++Index)
			{
				UDataLayer* DataLayer = SubSystem->GetDataLayer(DataLayers[Index]);
				if (DataLayer)
				{
					LayerName += DataLayer->GetDataLayerLabel().ToString();
				}
				else
				{
					LayerName += FText::Format(LOCTEXT("UnknownDataLayer", "**invalid: {0}**"), FText::FromString(DataLayers[Index].Name.ToString())).ToString();
				}

				if (Index < DataLayers.Num()-1)
				{
					LayerName += TEXT(", ");
				}
			}

			return FText::FromString(LayerName);
		}

		return FText();
	}

	FSlateColor GetTextColor() const
	{
		UMovieSceneDataLayerSection* Section = WeakSection.Get();
		if (Section)
		{
			switch (Section->GetDesiredState())
			{
			case EDataLayerState::Unloaded:  return FStyleColors::AccentRed;
			case EDataLayerState::Loaded:    return FStyleColors::AccentBlue;
			case EDataLayerState::Activated: return FStyleColors::AccentGreen;
			}
		}
		return FStyleColors::Foreground;
	}
private:

	TWeakObjectPtr<UMovieSceneDataLayerSection> WeakSection;
};

FDataLayerTrackEditor::FDataLayerTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{}

TSharedRef<ISequencerTrackEditor> FDataLayerTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FDataLayerTrackEditor>(InSequencer);
}

bool FDataLayerTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	ETrackSupport TrackSupported = InSequence ? InSequence->IsTrackSupported(UMovieSceneDataLayerTrack::StaticClass()) : ETrackSupport::NotSupported;
	return TrackSupported == ETrackSupport::Supported;
}

bool FDataLayerTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UMovieSceneDataLayerTrack::StaticClass();
}

const FSlateBrush* FDataLayerTrackEditor::GetIconBrush() const
{
	return FAppStyle::Get().GetBrush("Sequencer.Tracks.DataLayer");
}

TSharedRef<ISequencerSection> FDataLayerTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	UMovieSceneDataLayerSection* DataLayerSection = Cast<UMovieSceneDataLayerSection>(&SectionObject);
	check(SupportsType(SectionObject.GetOuter()->GetClass()) && DataLayerSection != nullptr);

	return MakeShared<FDataLayerSection>(DataLayerSection);
}

void FDataLayerTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddTrack", "Data Layer"),
		LOCTEXT("AddTrackToolTip", "Adds a new track that can load, activate or unload Data Layers in a World Partition world."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.Tracks.DataLayer"),
		FUIAction(FExecuteAction::CreateRaw(this, &FDataLayerTrackEditor::HandleAddTrack)));
}

TSharedPtr<SWidget> FDataLayerTrackEditor::BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params)
{
	return FSequencerUtilities::MakeAddButton(
		LOCTEXT("AddDataLayer_ButtonLabel", "Data Layer"),
		FOnGetContent::CreateSP(this, &FDataLayerTrackEditor::BuildAddDataLayerMenu, Track),
		Params.NodeIsHovered, GetSequencer());
}

UMovieSceneDataLayerSection* FDataLayerTrackEditor::AddNewSection(UMovieScene* MovieScene, UMovieSceneTrack* DataLayerTrack, EDataLayerState DesiredState)
{
	using namespace UE::MovieScene;

	const FScopedTransaction Transaction(LOCTEXT("AddDataLayerSection_Transaction", "Add Data Layer"));
	DataLayerTrack->Modify();

	UMovieSceneDataLayerSection* DataLayerSection = CastChecked<UMovieSceneDataLayerSection>(DataLayerTrack->CreateNewSection());
	DataLayerSection->SetDesiredState(DesiredState);

	TRange<FFrameNumber> SectionRange = MovieScene->GetPlaybackRange();
	DataLayerSection->InitialPlacement(DataLayerTrack->GetAllSections(), MovieScene->GetPlaybackRange().GetLowerBoundValue(), DiscreteSize(MovieScene->GetPlaybackRange()), true);
	DataLayerTrack->AddSection(*DataLayerSection);

	DataLayerSection->SetPreRollFrames((2.f * MovieScene->GetTickResolution()).RoundToFrame().Value);

	return DataLayerSection;
}

void FDataLayerTrackEditor::HandleAddTrack()
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
	if (FocusedMovieScene == nullptr || FocusedMovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddDataLayerTrack_Transaction", "Add Data Layer Track"));
	FocusedMovieScene->Modify();

	UMovieSceneDataLayerTrack* NewTrack = FocusedMovieScene->AddMasterTrack<UMovieSceneDataLayerTrack>();
	checkf(NewTrack, TEXT("Failed to create new data layer track."));

	UMovieSceneDataLayerSection* NewSection = AddNewSection(FocusedMovieScene, NewTrack, EDataLayerState::Activated);
	if (GetSequencer().IsValid())
	{
		GetSequencer()->OnAddTrack(NewTrack, FGuid());
	}
}

TSharedRef<SWidget> FDataLayerTrackEditor::BuildAddDataLayerMenu(UMovieSceneTrack* DataLayerTrack)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddActivatedDataLayer", "Activated"),
		LOCTEXT("AddActivatedDataLayer_Tip", "Instruct a data layer to be loaded and active."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(
			this, &FDataLayerTrackEditor::HandleAddNewSection, DataLayerTrack, EDataLayerState::Activated)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddLoadedDataLayer", "Loaded"),
		LOCTEXT("AddLoadedDataLayer_Tip", "Instruct a data layer to be loaded (but not active)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(
			this, &FDataLayerTrackEditor::HandleAddNewSection, DataLayerTrack, EDataLayerState::Loaded)));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddUnloadedDataLayer", "Unloaded"),
		LOCTEXT("AddUnloadedDataLayer_Tip", "Instruct a data layer to be unloaded for a duration."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(
			this, &FDataLayerTrackEditor::HandleAddNewSection, DataLayerTrack, EDataLayerState::Unloaded)));

	return MenuBuilder.MakeWidget();
}


void FDataLayerTrackEditor::HandleAddNewSection(UMovieSceneTrack* DataLayerTrack, EDataLayerState DesiredState)
{
	UMovieScene* FocusedMovieScene = GetFocusedMovieScene();
	if (FocusedMovieScene)
	{
		UMovieSceneDataLayerSection* NewSection = AddNewSection(FocusedMovieScene, DataLayerTrack, DesiredState);

		GetSequencer()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
		GetSequencer()->EmptySelection();
		GetSequencer()->SelectSection(NewSection);
		GetSequencer()->ThrobSectionSelection();
	}
}

#undef LOCTEXT_NAMESPACE
