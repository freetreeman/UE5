// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DerivedDataBuildJob.h"
#include "DerivedDataBuildKey.h"

namespace UE::DerivedData { struct FBuildSchedulerParams; }

namespace UE::DerivedData
{

/**
 * A build scheduler is responsible for deciding when and where a job executes in certain states.
 *
 * Jobs dispatch themselves to their scheduler when they are prepared to access limited resources
 * such as: memory, compute, storage, network. A scheduler may allow a job to execute immediately
 * or may queue it to execute later. A scheduler that uses a job queue is expected to execute the
 * jobs in priority order, respecting updates to priority.
 */
class IBuildScheduler
{
public:
	virtual ~IBuildScheduler() = default;

	/** Begin processing of the job by this scheduler. Always paired with EndJob. */
	virtual void BeginJob(IBuildJob* Job) {}

	/** End processing of the job by this scheduler. Always paired with BeginJob. */
	virtual void EndJob(IBuildJob* Job) {}

	/** Dispatch the job immediately if it is queued. May be called multiple times and/or concurrently. */
	virtual void CancelJob(IBuildJob* Job) {}

	/** Update the priority of the job if it is queued. May be called multiple times and/or concurrently. */
	virtual void UpdateJobPriority(IBuildJob* Job) {}

	/** Dispatch by calling BeginCacheQuery or SetOutput, either now or later. */
	virtual void DispatchCacheQuery(IBuildJob* Job, const FBuildSchedulerParams& Params) { Job->BeginCacheQuery(); }

	/** Dispatch by calling BeginCacheStore, either now or later. */
	virtual void DispatchCacheStore(IBuildJob* Job, const FBuildSchedulerParams& Params) { Job->BeginCacheStore(); }

	/** Dispatch by calling BeginResolveKey, either now or later. */
	virtual void DispatchResolveKey(IBuildJob* Job) { Job->BeginResolveKey(); }

	/** Dispatch by calling BeginResolveInputMeta, either now or later. */
	virtual void DispatchResolveInputMeta(IBuildJob* Job) { Job->BeginResolveInputMeta(); }

	/**
	 * Dispatch by calling BeginResolveInputData, SetOutput, or SkipExecuteRemote, either now or later.
	 *
	 * SkipExecuteRemote is only valid to call when MissingRemoteInputsSize is non-zero.
	 */
	virtual void DispatchResolveInputData(IBuildJob* Job, const FBuildSchedulerParams& Params) { Job->BeginResolveInputData(); }

	/** Dispatch by calling BeginExecuteRemote, SetOutput, or SkipExecuteRemote, either now or later. */
	virtual void DispatchExecuteRemote(IBuildJob* Job, const FBuildSchedulerParams& Params) { Job->BeginExecuteRemote(); }

	/** Dispatch by calling BeginExecuteLocal or SetOutput, either now or later. */
	virtual void DispatchExecuteLocal(IBuildJob* Job, const FBuildSchedulerParams& Params) { Job->BeginExecuteLocal(); }

	/** Set the output of the job. Always called once between BeginJob and EndJob unless canceled. */
	virtual void SetJobOutput(IBuildJob* Job, const FBuildSchedulerParams& Params, const FBuildOutput& Output) {}
};

/** Parameters that describe a build job to the build scheduler. */
struct FBuildSchedulerParams
{
	FBuildActionKey Key;

	/** Total size of constants and inputs, whether resolved or not. */
	uint64 TotalInputsSize = 0;
	/** Total size of constants and resolved inputs that are in memory now. */
	uint64 ResolvedInputsSize = 0;

	/** Total size of inputs that need to be resolved for local execution. Available in ResolveInputData. */
	uint64 MissingLocalInputsSize = 0;
	/** Total size of inputs that need to be resolved for remote execution. Available in ResolveInputData. */
	uint64 MissingRemoteInputsSize = 0;
};

} // UE::DerivedData
