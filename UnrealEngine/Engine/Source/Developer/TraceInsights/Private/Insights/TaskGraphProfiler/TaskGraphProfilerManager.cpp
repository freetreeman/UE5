// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskGraphProfilerManager.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/TasksProfiler.h"
#include "Async/TaskGraphInterfaces.h"

#include "Insights/InsightsStyle.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskTable.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskTimingTrack.h"
#include "Insights/TaskGraphProfiler/Widgets/STaskTableTreeView.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TaskGraphRelation.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/ViewModels/ThreadTrackEvent.h"
#include "Insights/Widgets/STimingProfilerWindow.h"
#include "Insights/Widgets/STimingView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "TaskGraphProfilerManager"

namespace Insights
{

const FName FTaskGraphProfilerTabs::TaskTableTreeViewTabID(TEXT("TaskTableTreeView"));

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTaskGraphProfilerManager> FTaskGraphProfilerManager::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTaskGraphProfilerManager> FTaskGraphProfilerManager::Get()
{
	return FTaskGraphProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FTaskGraphProfilerManager> FTaskGraphProfilerManager::CreateInstance()
{
	ensure(!FTaskGraphProfilerManager::Instance.IsValid());
	if (FTaskGraphProfilerManager::Instance.IsValid())
	{
		FTaskGraphProfilerManager::Instance.Reset();
	}

	FTaskGraphProfilerManager::Instance = MakeShared<FTaskGraphProfilerManager>(FInsightsManager::Get()->GetCommandList());

	return FTaskGraphProfilerManager::Instance;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTaskGraphProfilerManager::FTaskGraphProfilerManager(TSharedRef<FUICommandList> InCommandList)
	: bIsInitialized(false)
	, bIsAvailable(false)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::Initialize(IUnrealInsightsModule& InsightsModule)
{
	ensure(!bIsInitialized);
	if (bIsInitialized)
	{
		return;
	}
	bIsInitialized = true;

	InitializeColorCode();

	// Register tick functions.
	OnTick = FTickerDelegate::CreateSP(this, &FTaskGraphProfilerManager::Tick);
	OnTickHandle = FTicker::GetCoreTicker().AddTicker(OnTick, 0.0f);

	FOnRegisterMajorTabExtensions* TimingProfilerLayoutExtension = InsightsModule.FindMajorTabLayoutExtension(FInsightsManagerTabs::TimingProfilerTabId);
	if (TimingProfilerLayoutExtension)
	{
		TimingProfilerLayoutExtension->AddRaw(this, &FTaskGraphProfilerManager::RegisterTimingProfilerLayoutExtensions);
	}

	FInsightsManager::Get()->GetSessionChangedEvent().AddSP(this, &FTaskGraphProfilerManager::OnSessionChanged);
	OnSessionChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}
	bIsInitialized = false;

	FInsightsManager::Get()->GetSessionChangedEvent().RemoveAll(this);

	// Unregister tick function.
	FTicker::GetCoreTicker().RemoveTicker(OnTickHandle);

	FTaskGraphProfilerManager::Instance.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTaskGraphProfilerManager::~FTaskGraphProfilerManager()
{
	ensure(!bIsInitialized);

	if (TaskTimingSharedState.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(Insights::TimingViewExtenderFeatureName, TaskTimingSharedState.Get());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::RegisterMajorTabs(IUnrealInsightsModule& InsightsModule)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::UnregisterMajorTabs()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTaskGraphProfilerManager::Tick(float DeltaTime)
{
	// Check if session has task events (to spawn the tab), but not too often.
	if (!bIsAvailable && AvailabilityCheck.Tick())
	{
		bool bShouldBeAvailable = false;

		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());
			TSharedPtr<FTabManager> TabManagerShared = TimingTabManager.Pin();
			if (TasksProvider && TasksProvider->GetNumTasks() > 0 && TabManagerShared.IsValid())
			{
				TSharedPtr<STimingProfilerWindow> Window = FTimingProfilerManager::Get()->GetProfilerWindow();
				if (!Window.IsValid())
				{
					return true;
				}

				TSharedPtr<STimingView> TimingView = Window->GetTimingView();
				if (!TimingView.IsValid())
				{
					return true;
				}

				bIsAvailable = true;

				if (!TaskTimingSharedState.IsValid())
				{
					TaskTimingSharedState = MakeShared<FTaskTimingSharedState>(TimingView.Get());
					IModularFeatures::Get().RegisterModularFeature(Insights::TimingViewExtenderFeatureName, TaskTimingSharedState.Get());
				}
				TabManagerShared->TryInvokeTab(FTaskGraphProfilerTabs::TaskTableTreeViewTabID);
			}

			if (Session->IsAnalysisComplete())
			{
				// Never check again during this session.
				AvailabilityCheck.Disable();
			}
		}
		else
		{
			// Do not check again until the next session changed event (see OnSessionChanged).
			AvailabilityCheck.Disable();
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::OnSessionChanged()
{
	bIsAvailable = false;
	if (FInsightsManager::Get()->GetSession().IsValid())
	{
		AvailabilityCheck.Enable(0.5);
	}
	else
	{
		AvailabilityCheck.Disable();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::RegisterTimingProfilerLayoutExtensions(FInsightsMajorTabExtender& InOutExtender)
{
	TimingTabManager = InOutExtender.GetTabManager();

	FInsightsMinorTabConfig& MinorTabConfig = InOutExtender.AddMinorTabConfig();
	MinorTabConfig.TabId = FTaskGraphProfilerTabs::TaskTableTreeViewTabID;
	MinorTabConfig.TabLabel = LOCTEXT("TaskTableTreeViewTabTitle", "Tasks");
	MinorTabConfig.TabTooltip = LOCTEXT("TaskTableTreeViewTabTitleTooltip", "Opens the Task Table Tree View tab, that allows Task Graph profilling.");
	MinorTabConfig.TabIcon = FSlateIcon(FSlateIcon(FInsightsStyle::GetStyleSetName(), "TimersView.Icon.Small"));
	MinorTabConfig.OnSpawnTab = FOnSpawnTab::CreateRaw(this, &FTaskGraphProfilerManager::SpawnTab_TaskTableTreeView);
	MinorTabConfig.CanSpawnTab = FCanSpawnTab::CreateRaw(this, &FTaskGraphProfilerManager::CanSpawnTab_TaskTableTreeView);

	InOutExtender.GetLayoutExtender().ExtendLayout(FTimingProfilerTabs::StatsCountersID
		, ELayoutExtensionPosition::After
		, FTabManager::FTab(FTaskGraphProfilerTabs::TaskTableTreeViewTabID, ETabState::ClosedTab));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> FTaskGraphProfilerManager::SpawnTab_TaskTableTreeView(const FSpawnTabArgs& Args)
{
	TSharedRef<FTaskTable> TaskTable = MakeShared<FTaskTable>();
	TaskTable->Reset();

	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.ShouldAutosize(false)
		.TabRole(ETabRole::PanelTab)
		[
			SAssignNew(TaskTableTreeView, STaskTableTreeView, TaskTable)
		];

	DockTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateRaw(this, &FTaskGraphProfilerManager::OnTaskTableTreeViewTabClosed));

	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTaskGraphProfilerManager::CanSpawnTab_TaskTableTreeView(const FSpawnTabArgs& Args)
{
	return bIsAvailable;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::OnTaskTableTreeViewTabClosed(TSharedRef<SDockTab> TabBeingClosed)
{
	TaskTableTreeView.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::ShowTaskRelations(const TraceServices::FTaskInfo* Task, const TraceServices::ITasksProvider* TasksProvider, const FThreadTrackEvent* InSelectedEvent)
{
	check(Task != nullptr);

	if (!bShowRelations)
	{
		return;
	}

	auto GetSingleTaskRelationsForAll = [this, TasksProvider, InSelectedEvent](const TArray< TraceServices::FTaskInfo::FRelationInfo>& Collection)
	{
		for (const TraceServices::FTaskInfo::FRelationInfo& Relation : Collection)
		{
			const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(Relation.RelativeId);
			if (Task != nullptr)
			{
				GetSingleTaskRelations(Task, TasksProvider, InSelectedEvent);
			}
		}
	};

	if (bShowPrerequisites)
	{
		GetSingleTaskRelationsForAll(Task->Prerequisites);
	}
	GetSingleTaskRelations(Task, TasksProvider, InSelectedEvent);
	if (bShowNestedTasks)
	{
		GetSingleTaskRelationsForAll(Task->NestedTasks);
	}
	if (bShowSubsequents)
	{
		GetSingleTaskRelationsForAll(Task->Subsequents);
	}
}

void FTaskGraphProfilerManager::GetSingleTaskRelations(const TraceServices::FTaskInfo* Task, const TraceServices::ITasksProvider* TasksProvider, const FThreadTrackEvent* InSelectedEvent)
{
	const int32 MaxTasksToShow = 30;

	if (Task->CreatedTimestamp != Task->LaunchedTimestamp || Task->CreatedThreadId != Task->LaunchedThreadId)
	{
		AddRelation(InSelectedEvent, Task->CreatedTimestamp, Task->CreatedThreadId, Task->LaunchedTimestamp, Task->LaunchedThreadId, ETaskEventType::Created);
	}

	if (Task->LaunchedTimestamp != Task->ScheduledTimestamp || Task->LaunchedThreadId != Task->ScheduledThreadId)
	{
		AddRelation(InSelectedEvent, Task->LaunchedTimestamp, Task->LaunchedThreadId, Task->ScheduledTimestamp, Task->ScheduledThreadId, ETaskEventType::Launched);
	}
	int32 NumPrerequisitesToShow = FMath::Min(Task->Prerequisites.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumPrerequisitesToShow; ++i)
	{
		const TraceServices::FTaskInfo* Prerequisite = TasksProvider->TryGetTask(Task->Prerequisites[i].RelativeId);
		check(Prerequisite != nullptr);
		AddRelation(InSelectedEvent, Prerequisite->CompletedTimestamp, Prerequisite->CompletedThreadId, Task->ScheduledTimestamp, Task->ScheduledThreadId, ETaskEventType::Prerequisite);
	}

	int32 ExecutionStartedDepth = GetDepthOfTaskExecution(Task->StartedTimestamp, Task->FinishedTimestamp, Task->StartedThreadId);
	AddRelation(InSelectedEvent, Task->ScheduledTimestamp, Task->ScheduledThreadId, -1, Task->StartedTimestamp, Task->StartedThreadId, ExecutionStartedDepth, ETaskEventType::Scheduled);

	int32 NumNestedToShow = FMath::Min(Task->NestedTasks.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumNestedToShow; ++i)
	{
		const TraceServices::FTaskInfo::FRelationInfo& RelationInfo = Task->NestedTasks[i];
		const TraceServices::FTaskInfo* NestedTask = TasksProvider->TryGetTask(RelationInfo.RelativeId);
		check(NestedTask != nullptr);

		int32 NestedExecutionStartedDepth = GetDepthOfTaskExecution(Task->StartedTimestamp, Task->FinishedTimestamp, Task->StartedThreadId);

		AddRelation(InSelectedEvent, RelationInfo.Timestamp, Task->StartedThreadId, -1, NestedTask->StartedTimestamp,  NestedTask->StartedThreadId, NestedExecutionStartedDepth,  ETaskEventType::AddedNested);

		AddRelation(InSelectedEvent, NestedTask->CompletedTimestamp, NestedTask->CompletedThreadId, NestedTask->CompletedTimestamp, Task->StartedThreadId, ETaskEventType::NestedCompleted);
	}

	int32 NumSubsequentsToShow = FMath::Min(Task->Subsequents.Num(), MaxTasksToShow);
	for (int32 i = 0; i != NumSubsequentsToShow; ++i)
	{
		const TraceServices::FTaskInfo* Subsequent = TasksProvider->TryGetTask(Task->Subsequents[i].RelativeId);
		check(Subsequent != nullptr);
		if (Task->CompletedTimestamp < Subsequent->ScheduledTimestamp)
		{
			AddRelation(InSelectedEvent, Task->CompletedTimestamp, Task->CompletedThreadId, Subsequent->ScheduledTimestamp, Subsequent->ScheduledThreadId, ETaskEventType::Subsequent);
		}
	}

	if (Task->FinishedTimestamp != Task->CompletedTimestamp || Task->CompletedThreadId != Task->StartedThreadId)
	{
		AddRelation(InSelectedEvent, Task->FinishedTimestamp, Task->StartedThreadId, ExecutionStartedDepth, Task->CompletedTimestamp, Task->StartedThreadId, -1,  ETaskEventType::Completed);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::ShowTaskRelations(const FThreadTrackEvent* InSelectedEvent, uint32 ThreadId)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

	if (TasksProvider == nullptr)
	{
		return;
	}

	const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(ThreadId, InSelectedEvent->GetStartTime());
	ClearTaskRelations();

	if (Task != nullptr)
	{
		ShowTaskRelations(Task, TasksProvider, InSelectedEvent);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::ShowTaskRelations(uint32 TaskId)
{
	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

	if (TasksProvider == nullptr)
	{
		return;
	}

	const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(TaskId);
	ClearTaskRelations();

	if (Task != nullptr)
	{
		ShowTaskRelations(Task, TasksProvider, nullptr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::OnWindowClosedEvent()
{
	TSharedPtr<FTabManager> TimingTabManagerSharedPtr = TimingTabManager.Pin();

	if (TimingTabManagerSharedPtr.IsValid())
	{
		TSharedPtr<SDockTab> Tab = TimingTabManagerSharedPtr->FindExistingLiveTab(FTaskGraphProfilerTabs::TaskTableTreeViewTabID);
		if (Tab.IsValid())
		{
			Tab->RequestCloseTab();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::InitializeColorCode()
{
	ColorCode[static_cast<uint32>(ETaskEventType::Created)] = FLinearColor::Yellow;
	ColorCode[static_cast<uint32>(ETaskEventType::Launched)] = FLinearColor::Green;
	ColorCode[static_cast<uint32>(ETaskEventType::Prerequisite)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::Scheduled)] = FLinearColor::Blue;
	ColorCode[static_cast<uint32>(ETaskEventType::Started)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::AddedNested)] = FLinearColor::Blue;
	ColorCode[static_cast<uint32>(ETaskEventType::NestedCompleted)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::Subsequent)] = FLinearColor::Red;
	ColorCode[static_cast<uint32>(ETaskEventType::Completed)] = FLinearColor::Yellow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FTaskGraphProfilerManager::GetColorForTaskEvent(ETaskEventType InEvent)
{
	check(InEvent < ETaskEventType::NumTaskEventTypes);
	return ColorCode[static_cast<uint32>(InEvent)];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::AddRelation(const FThreadTrackEvent* InSelectedEvent, double SourceTimestamp, uint32 SourceThreadId, double TargetTimestamp, uint32 TargetThreadId, ETaskEventType Type)
{
	AddRelation(InSelectedEvent, SourceTimestamp, SourceThreadId, -1, TargetTimestamp, TargetThreadId, -1, Type);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::AddRelation(const FThreadTrackEvent* InSelectedEvent, double SourceTimestamp, uint32 SourceThreadId, int32 SourceDepth, double TargetTimestamp, uint32 TargetThreadId, int32 TargetDepth, ETaskEventType Type)
{
	if (SourceTimestamp == TraceServices::FTaskInfo::InvalidTimestamp || TargetTimestamp == TraceServices::FTaskInfo::InvalidTimestamp)
	{
		return;
	}

	TSharedPtr<class STimingProfilerWindow> TimingWindow = FTimingProfilerManager::Get()->GetProfilerWindow();
	if (!TimingWindow.IsValid())
	{
		return;
	}

	TSharedPtr<STimingView> TimingView = TimingWindow->GetTimingView();
	if (!TimingView.IsValid())
	{
		return;
	}

	TSharedPtr<FThreadTimingSharedState> ThreadSharedState = TimingView->GetThreadTimingSharedState();

	TUniquePtr<ITimingEventRelation> Relation = MakeUnique<FTaskGraphRelation>(SourceTimestamp, SourceThreadId, TargetTimestamp, TargetThreadId, Type);
	FTaskGraphRelation* TaskRelationPtr = StaticCast<FTaskGraphRelation*>(Relation.Get());

	// If we have a valid event, we can try getting the tracks and depths using this faster approach.
	if (InSelectedEvent)
	{
		TSharedPtr<const FThreadTimingTrack> EventTrack = StaticCastSharedRef<const FThreadTimingTrack>(InSelectedEvent->GetTrack());
		uint32 ThreadId = EventTrack->GetThreadId();

		if (TaskRelationPtr->GetSourceThreadId() == ThreadId)
		{
			TaskRelationPtr->SetSourceTrack(EventTrack);
			TaskRelationPtr->SetSourceDepth(SourceDepth > 0 ? SourceDepth : EventTrack->GetDepthAt(TaskRelationPtr->GetSourceTime()) - 1);
		}

		if (TaskRelationPtr->GetTargetThreadId() == ThreadId)
		{
			TaskRelationPtr->SetTargetTrack(EventTrack);
			TaskRelationPtr->SetTargetDepth(TargetDepth > 0 ? TargetDepth : EventTrack->GetDepthAt(TaskRelationPtr->GetTargetTime()) - 1);
		}
	}

	if(!TaskRelationPtr->GetSourceTrack().IsValid())
	{
		TSharedPtr<FCpuTimingTrack> Track = ThreadSharedState->GetCpuTrack(TaskRelationPtr->GetSourceThreadId());
		if (Track.IsValid())
		{
			TaskRelationPtr->SetSourceTrack(Track);
			TaskRelationPtr->SetSourceDepth(SourceDepth > 0 ? SourceDepth : Track->GetDepthAt(TaskRelationPtr->GetSourceTime()) - 1);
		}
	}

	if (!TaskRelationPtr->GetTargetTrack().IsValid())

	{
		TSharedPtr<FCpuTimingTrack> Track = ThreadSharedState->GetCpuTrack(TaskRelationPtr->GetTargetThreadId());
		if (Track.IsValid())
		{
			TaskRelationPtr->SetTargetTrack(Track);
			TaskRelationPtr->SetTargetDepth(TargetDepth > 0 ? TargetDepth : Track->GetDepthAt(TaskRelationPtr->GetTargetTime()) - 1);
		}
	}

	if (TaskRelationPtr->GetSourceTrack().IsValid() && TaskRelationPtr->GetTargetTrack().IsValid())
	{
		TimingView->AddRelation(Relation);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::ClearTaskRelations()
{
	TSharedPtr<class STimingProfilerWindow> TimingWindow = FTimingProfilerManager::Get()->GetProfilerWindow();
	if (!TimingWindow.IsValid())
	{
		return;
	}

	TSharedPtr<STimingView> TimingView = TimingWindow->GetTimingView();
	if (!TimingView.IsValid())
	{
		return;
	}

	TArray<TUniquePtr<ITimingEventRelation>>& Relations = TimingView->EditCurrentRelations();
	Relations.RemoveAll([](TUniquePtr<ITimingEventRelation>& Relations)
		{
			return Relations->Is<FTaskGraphRelation>();
		});
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskGraphProfilerManager::SetShowRelations(bool bInValue)
{
	bShowRelations = bInValue;
	if (!bShowRelations)
	{
		TaskTimingSharedState->SetTaskId(FTaskTimingTrack::InvalidTaskId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FTaskGraphProfilerManager::GetDepthOfTaskExecution(double TaskStartedTime, double TaskFinishedTime, uint32 ThreadId)
{
	int32 Depth = -1;
	TSharedPtr<class STimingProfilerWindow> TimingWindow = FTimingProfilerManager::Get()->GetProfilerWindow();
	if (!TimingWindow.IsValid())
	{
		return Depth;
	}

	TSharedPtr<STimingView> TimingView = TimingWindow->GetTimingView();
	if (!TimingView.IsValid())
	{
		return Depth;
	}

	TSharedPtr<FThreadTimingSharedState> ThreadSharedState = TimingView->GetThreadTimingSharedState();

	TSharedPtr<FCpuTimingTrack> Track = ThreadSharedState->GetCpuTrack(ThreadId);

	if (!Track.IsValid())
	{
		return Depth;
	}

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid() && TraceServices::ReadTimingProfilerProvider(*Session.Get()))
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(*Session.Get());

		TimingProfilerProvider.ReadTimeline(Track->GetTimelineIndex(),
			[TaskStartedTime, TaskFinishedTime, &Depth](const TraceServices::ITimingProfilerProvider::Timeline& Timeline)
			{
				Timeline.EnumerateEventsDownSampled(TaskStartedTime, TaskFinishedTime, 0, [TaskStartedTime, &Depth](bool IsEnter, double Time, const TraceServices::FTimingProfilerEvent& Event)
					{
						if (Time < TaskStartedTime)
						{
							check(IsEnter);
							++Depth;
							return TraceServices::EEventEnumerate::Continue;
						}

						if (IsEnter)
						{
							++Depth;
						}

						return TraceServices::EEventEnumerate::Stop;
					});
			});
	}

	return Depth;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
