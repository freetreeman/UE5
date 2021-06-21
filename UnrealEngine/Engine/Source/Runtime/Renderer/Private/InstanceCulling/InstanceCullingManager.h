// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderGraphResources.h"
#include "SceneManagement.h"
#include "../Nanite/NaniteRender.h"
#include "InstanceCulling/InstanceCullingContext.h"

class FGPUScene;

class FInstanceCullingIntermediate
{
public:
	/**
	 * One bit per Instance per registered view produced by CullInstances.
	 */
	FRDGBufferRef VisibleInstanceFlags = nullptr;
	
	int32 NumInstances = 0;
	int32 NumViews = 0;

	TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> DummyUniformBuffer;
};

struct FInstanceCullingResult
{
	//TRefCountPtr<FRDGPooledBuffer> InstanceIdsBuffer;
	FRDGBufferRef DrawIndirectArgsBuffer = nullptr;
	FRDGBufferRef InstanceIdOffsetBuffer = nullptr;
	// Offset (in items, not bytes or something) for both buffers to start fetching data at, used when batching multiple culling jobs in the same buffer
	uint32 DrawCommandDataOffset = 0U;
	TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> UniformBuffer = nullptr;

	//FRHIBuffer* GetDrawIndirectArgsBufferRHI() const { return DrawIndirectArgsBuffer.IsValid() ? DrawIndirectArgsBuffer->GetVertexBufferRHI() : nullptr; }
	//FRHIBuffer* GetInstanceIdOffsetBufferRHI() const { return InstanceIdOffsetBuffer.IsValid() ? InstanceIdOffsetBuffer->GetVertexBufferRHI() : nullptr; }
	void GetDrawParameters(FInstanceCullingDrawParams &OutParams) const
	{
		// GPUCULL_TODO: Maybe get dummy buffers?
		OutParams.DrawIndirectArgsBuffer = DrawIndirectArgsBuffer;
		OutParams.InstanceIdOffsetBuffer = InstanceIdOffsetBuffer;
		OutParams.DrawCommandDataOffset = DrawCommandDataOffset;
		OutParams.InstanceCulling = UniformBuffer;
	}

	static void CondGetDrawParameters(const FInstanceCullingResult* InstanceCullingResult, FInstanceCullingDrawParams& OutParams)
	{
		if (InstanceCullingResult)
		{
			InstanceCullingResult->GetDrawParameters(OutParams);
		}
		else
		{
			OutParams.DrawIndirectArgsBuffer = nullptr;
			OutParams.InstanceIdOffsetBuffer = nullptr;
			OutParams.DrawCommandDataOffset = 0U;
			OutParams.InstanceCulling = nullptr;
		}
	}
};

struct FBatchedInstanceCullingScratchSpace
{
	/** Whether we defer and batch GPU instance culling work throughout a frame. */
	bool bBatchingActive = false;

	FRDGBufferRef DrawIndirectArgsBuffer = nullptr;
	FRDGBufferRef InstanceIdOffsetBuffer = nullptr;
	TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> UniformBuffer = nullptr;

	/** GPU instance culling input data merged from multiple batches throughout a frame. */
	FInstanceCullingContextMerged MergedContext;

	/** Batches of GPU instance culling input data. */
	TArray<FInstanceCullingContext::FBatchItem, SceneRenderingAllocator> Batches;
};

/**
 * Manages allocation of indirect arguments and culling jobs for all instanced draws (use the GPU Scene culling).
 */
class FInstanceCullingManager
{
public:
	~FInstanceCullingManager();

	bool IsEnabled() const { return bIsEnabled; }

	// Register a view for culling, returns integer ID of the view.
	int32 RegisterView(const Nanite::FPackedViewParams& Params);
	
	// Helper to translate from view info, extracts the needed data for setting up instance culling.
	int32 RegisterView(const FViewInfo& ViewInfo);

	/**
	 * Run:
	 *   AFTER views have been initialized and registered (including shadow views), 
	 *   AFTER after GPUScene is updated, but
	 *   BEFORE rendering commands are submitted.
	 */
	void CullInstances(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene);

	FInstanceCullingManager(bool bInIsEnabled)
	: bIsEnabled(bInIsEnabled)
	{
	}
	const TRDGUniformBufferRef<FInstanceCullingGlobalUniforms> GetDummyInstanceCullingUniformBuffer() const { return CullingIntermediate.DummyUniformBuffer; }
	
	/** Starts batching GPU instance culling work items if possible. */
	void BeginDeferredCulling(FRDGBuilder& GraphBuilder, FGPUScene& GPUScene);

	/** Whether we are actively batching GPU instance culling work. */
	bool IsDeferredCullingActive() const { return BatchedCullingScratch.bBatchingActive; }

	// Populated by CullInstances, used when performing final culling & rendering 
	FInstanceCullingIntermediate CullingIntermediate;


private:
	friend class FInstanceCullingContext;

	// Polulated by FInstanceCullingContext::BuildRenderingCommandsDeferred, used to hold instance culling related data that needs to be passed around
	FBatchedInstanceCullingScratchSpace BatchedCullingScratch;

	FInstanceCullingManager() = delete;
	FInstanceCullingManager(FInstanceCullingManager &) = delete;

	TArray<Nanite::FPackedView> CullingViews;
	bool bIsEnabled;
};

