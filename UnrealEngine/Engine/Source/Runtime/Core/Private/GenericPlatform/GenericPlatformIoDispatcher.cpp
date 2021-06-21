// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatformIoDispatcher.h"
#include "IO/IoDispatcherFileBackend.h"
#include "Async/AsyncFileHandle.h"
#include "HAL/Event.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/RunnableThread.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"

//PRAGMA_DISABLE_OPTIMIZATION


TRACE_DECLARE_INT_COUNTER_EXTERN(IoDispatcherFileBackendSequentialReads);
TRACE_DECLARE_INT_COUNTER_EXTERN(IoDispatcherFileBackendForwardSeeks);
TRACE_DECLARE_INT_COUNTER_EXTERN(IoDispatcherFileBackendBackwardSeeks);
TRACE_DECLARE_INT_COUNTER_EXTERN(IoDispatcherFileBackendSwitchContainerSeeks);
TRACE_DECLARE_MEMORY_COUNTER_EXTERN(IoDispatcherFileBackendTotalSeekDistance);
TRACE_DECLARE_INT_COUNTER_EXTERN(IoDispatcherFileBackendFileSystemRequests);
TRACE_DECLARE_MEMORY_COUNTER_EXTERN(IoDispatcherFileBackendFileSystemTotalBytesRead);

FGenericFileIoStoreEventQueue::FGenericFileIoStoreEventQueue()
	: ServiceEvent(FPlatformProcess::GetSynchEventFromPool())
{
}

FGenericFileIoStoreEventQueue::~FGenericFileIoStoreEventQueue()
{
	FPlatformProcess::ReturnSynchEventToPool(ServiceEvent);
}

void FGenericFileIoStoreEventQueue::ServiceNotify()
{
	ServiceEvent->Trigger();
}

void FGenericFileIoStoreEventQueue::ServiceWait()
{
	ServiceEvent->Wait();
}

FGenericFileIoStoreImpl::FGenericFileIoStoreImpl(FGenericFileIoStoreEventQueue& InEventQueue, FFileIoStoreBufferAllocator& InBufferAllocator, FFileIoStoreBlockCache& InBlockCache)
	: EventQueue(InEventQueue)
	, BufferAllocator(InBufferAllocator)
	, BlockCache(InBlockCache)
{
}

FGenericFileIoStoreImpl::~FGenericFileIoStoreImpl()
{
}

bool FGenericFileIoStoreImpl::OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize)
{
	IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
	int64 FileSize = Ipf.FileSize(ContainerFilePath);
	if (FileSize < 0)
	{
		return false;
	}
	IFileHandle* FileHandle = Ipf.OpenReadNoBuffering(ContainerFilePath);
	if (!FileHandle)
	{
		return false;
	}
	ContainerFileHandle = reinterpret_cast<UPTRINT>(FileHandle);
	ContainerFileSize = uint64(FileSize);
	return true;
}

void FGenericFileIoStoreImpl::CloseContainer(uint64 ContainerFileHandle)
{
	check(ContainerFileHandle);
	FFileIoStats::OnCloseHandle(ContainerFileHandle);
	IFileHandle* FileHandle = reinterpret_cast<IFileHandle*>(ContainerFileHandle);
	delete FileHandle;
}

bool FGenericFileIoStoreImpl::StartRequests(FFileIoStoreRequestQueue& RequestQueue)
{
	FFileIoStoreReadRequest* NextRequest = RequestQueue.Pop();
	if (!NextRequest)
	{
		return false;
	}

	if (NextRequest->bCancelled)
	{
		{
			FScopeLock _(&CompletedRequestsCritical);
			CompletedRequests.Add(NextRequest);
		}
		WakeUpDispatcherThreadDelegate->Execute();
		return true;
	}

	uint8* Dest;
	if (!NextRequest->ImmediateScatter.Request)
	{
		NextRequest->Buffer = BufferAllocator.AllocBuffer();
		if (!NextRequest->Buffer)
		{
			RequestQueue.Push(*NextRequest);
			return false;
		}
		Dest = NextRequest->Buffer->Memory;
	}
	else
	{
		Dest = NextRequest->ImmediateScatter.Request->GetIoBuffer().Data() + NextRequest->ImmediateScatter.DstOffset;
	}
	
	if (!BlockCache.Read(NextRequest))
	{
		IFileHandle* FileHandle = reinterpret_cast<IFileHandle*>(static_cast<UPTRINT>(NextRequest->FileHandle));
		 
		FFileIoStats::OnFilesystemReadStarted(NextRequest->FileHandle, NextRequest->Offset, NextRequest->Size);
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ReadBlockFromFile);
			NextRequest->bFailed = true;
			int32 RetryCount = 0;
			TRACE_COUNTER_INCREMENT(IoDispatcherFileBackendFileSystemRequests);
			while (RetryCount++ < 10)
			{
				if (!FileHandle->Seek(NextRequest->Offset))
				{
					UE_LOG(LogIoDispatcher, Warning, TEXT("Failed seeking to offset %lld (Retries: %d)"), NextRequest->Offset, (RetryCount - 1));
					continue;
				}
				if (!FileHandle->Read(Dest, NextRequest->Size))
				{
					UE_LOG(LogIoDispatcher, Warning, TEXT("Failed reading %lld bytes at offset %lld (Retries: %d)"), NextRequest->Size, NextRequest->Offset, (RetryCount - 1));
					continue;
				}
				NextRequest->bFailed = false;
				TRACE_COUNTER_ADD(IoDispatcherFileBackendFileSystemTotalBytesRead, NextRequest->Size);
				BlockCache.Store(NextRequest);
				break;
			}
		}
		FFileIoStats::OnFilesystemReadsComplete(NextRequest->Size);
	}
	{
		FScopeLock _(&CompletedRequestsCritical);
		CompletedRequests.Add(NextRequest);
	}
	WakeUpDispatcherThreadDelegate->Execute();
	return true;
}

void FGenericFileIoStoreImpl::GetCompletedRequests(FFileIoStoreReadRequestList& OutRequests)
{
	FScopeLock _(&CompletedRequestsCritical);
	OutRequests.AppendSteal(CompletedRequests);
}
