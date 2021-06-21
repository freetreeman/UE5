// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IRewindDebugger.h"
#include "BindableProperty.h"
#include "UObject/WeakObjectPtr.h"
#include "RewindDebuggerModule.h"


namespace TraceServices
{
	class IAnalysisSession;
}

// Singleton class that handles the logic for the Rewind Debugger
// handles:
//  Playback/Scrubbing state
//  Start/Stop recording
//  Keeping track of the current Debug Target actor, and outputing a list of it's Components for the UI

class FRewindDebugger : public IRewindDebugger
{
public:
	FRewindDebugger();
	virtual ~FRewindDebugger();

	// IRewindDebugger interface
	virtual double CurrentTraceTime() const override { return TraceTime.Get(); }
	virtual const TraceServices::IAnalysisSession* GetAnalysisSession() const override;
	virtual uint64 GetTargetActorId() const override;
	virtual bool GetTargetActorPosition(FVector& OutPosition) const override;
	virtual UWorld* GetWorldToVisualize() const override;

	// create singleton instance
	static void Initialize();

	// destroy singleton instance
	static void Shutdown();

	// get singleton instance
	static FRewindDebugger* Instance() { return InternalInstance; }

	// Start a new Recording:  Start tracing Object + Animation data, increment the current recording index, and reset the recording elapsed time to 0
	void StartRecording();

	bool IsRecording() const { return bRecording; }
	bool CanStartRecording() const { return !IsRecording() && bPIESimulating; }

	bool AutoRecord() const { return bAutoRecord; }
	void SetAutoRecord(bool value) { bAutoRecord = value; }

	// Stop recording: Stop tracing Object + Animation Data.
	void StopRecording();
	bool CanStopRecording() const { return IsRecording(); }

	// VCR controls

	bool CanPause() const;
	void Pause();

	bool CanPlay() const;
	void Play();
	bool IsPlaying() const;

	bool CanPlayReverse() const;
	void PlayReverse();

	void ScrubToStart();
	void ScrubToEnd();
	void StepForward();
	void StepBackward();

	bool CanScrub() const;
	void ScrubToTime(float ScrubTime, bool bIsScrubbing);
	float GetScrubTime() { return CurrentScrubTime; }

	// Tick function: While recording, update recording duration.  While paused, and we have recorded data, update skinned mesh poses for the current frame, and handle playback.
	void Tick(float DeltaTime);

	// update the list of components for the currently selected debug target
	void RefreshDebugComponents();

	TArray<TSharedPtr<FDebugObjectInfo>>& GetDebugComponents() { return DebugComponents; };

	DECLARE_DELEGATE(FOnComponentListChanged)
	void OnComponentListChanged(const FOnComponentListChanged& ComponentListChangedCallback);

	DECLARE_DELEGATE_OneParam( FOnTrackCursor, bool)
	void OnTrackCursor(const FOnTrackCursor& TrackCursorCallback);

	TBindableProperty<double>* GetTraceTimeProperty() { return &TraceTime; }
	TBindableProperty<float>* GetRecordingDurationProperty() { return &RecordingDuration; }
	TBindableProperty<FString, BindingType_Out>* GetDebugTargetActorProperty() { return &DebugTargetActor; }

private:
	void OnPIEStarted(bool bSimulating);
	void OnPIEPaused(bool bSimulating);
	void OnPIEResumed(bool bSimulating);
	void OnPIEStopped(bool bSimulating);
	void OnPIESingleStepped(bool bSimulating);


	bool UpdateComponentList(uint64 ParentId, TArray<TSharedPtr<FDebugObjectInfo>>& NewComponentList);
	void SetCurrentScrubTime(float Time);
	void UpdateTraceTime();

	TBindableProperty<double> TraceTime;
	TBindableProperty<float> RecordingDuration;
	TBindableProperty<FString, BindingType_Out> DebugTargetActor;

	enum class EControlState : int8
	{
		Play,
		PlayReverse,
		Pause
	};

	EControlState ControlState;

	FOnComponentListChanged ComponentListChangedDelegate;
	FOnTrackCursor TrackCursorDelegate;

	bool bPIEStarted;
	bool bPIESimulating;

	bool bAutoRecord;
	bool bRecording;

	float PlaybackRate;
	float CurrentScrubTime;
	uint16 RecordingIndex;

	static FRewindDebugger* InternalInstance;

	TArray<TSharedPtr<FDebugObjectInfo>> DebugComponents;

	struct FMeshComponentResetData
	{
		TWeakObjectPtr<USkeletalMeshComponent> Component;
		FTransform RelativeTransform;
	};

	TMap<uint64, FMeshComponentResetData> MeshComponentsToReset;

	mutable class IUnrealInsightsModule *UnrealInsightsModule;
	FDelegateHandle TickerHandle;

	bool bTargetActorPositionValid;
	FVector TargetActorPosition;
};
