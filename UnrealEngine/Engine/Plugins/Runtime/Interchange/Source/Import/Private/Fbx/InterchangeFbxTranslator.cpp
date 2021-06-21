// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Fbx/InterchangeFbxTranslator.h"

#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "InterchangeDispatcher.h"
#include "InterchangeDispatcherTask.h"
#include "InterchangeManager.h"
#include "InterchangeImportLog.h"
#include "Mesh/InterchangeSkeletalMeshPayload.h"
#include "Mesh/InterchangeStaticMeshPayload.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/LargeMemoryReader.h"
#include "Texture/InterchangeTexturePayloadData.h"
#include "UObject/GCObjectScopeGuard.h"

UInterchangeFbxTranslator::UInterchangeFbxTranslator(const class FObjectInitializer& ObjectInitializer)
{
	Dispatcher = nullptr;
}

bool UInterchangeFbxTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	const bool bIncludeDot = false;
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename(), bIncludeDot);
	FString FbxExtension = (TEXT("fbx"));
	return FbxExtension.StartsWith(Extension,ESearchCase::IgnoreCase);
}

bool UInterchangeFbxTranslator::Translate(const UInterchangeSourceData* SourceData, UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	FString Filename = SourceData->GetFilename();
	if (!FPaths::FileExists(Filename))
	{
		return false;
	}

	if(!Dispatcher.IsValid())
	{
		//Dispatch an Interchange worker by using the InterchangeDispatcher
		//Build Result folder
		FGuid RandomGuid;
		FPlatformMisc::CreateGuid(RandomGuid);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const FString ProjectSavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		const FString RamdomGuidDir = RandomGuid.ToString(EGuidFormats::Base36Encoded);
		if (!PlatformFile.DirectoryExists(*ProjectSavedDir))
		{
			PlatformFile.CreateDirectory(*ProjectSavedDir);
		}
		const FString InterchangeDir = FPaths::Combine(ProjectSavedDir, TEXT("Interchange"));
		if (!PlatformFile.DirectoryExists(*InterchangeDir))
		{
			PlatformFile.CreateDirectory(*InterchangeDir);
		}
		const FString ResultFolder = FPaths::Combine(InterchangeDir, RamdomGuidDir);
		if (!PlatformFile.DirectoryExists(*ResultFolder))
		{
			PlatformFile.CreateDirectory(*ResultFolder);
		}

		//Create the dispatcher
		Dispatcher = MakeUnique<UE::Interchange::FInterchangeDispatcher>(ResultFolder);

		if(ensure(Dispatcher.IsValid()))
		{
			Dispatcher->StartProcess();
		}
	}

	if(!Dispatcher.IsValid())
	{
		return false;
	}

	//Create a json command to read the fbx file
	FString JsonCommand = CreateLoadFbxFileCommand(SourceData->GetFilename());
	int32 TaskIndex = Dispatcher->AddTask(JsonCommand);

	//Blocking call until all tasks are executed
	Dispatcher->WaitAllTaskToCompleteExecution();
		
	UE::Interchange::ETaskState TaskState;
	FString JsonResult;
	TArray<FString> JSonMessages;
	Dispatcher->GetTaskState(TaskIndex, TaskState, JsonResult, JSonMessages);

	//TODO: Parse the JSonMessage and add the message to the interchange not yet develop error messaging

	if(TaskState != UE::Interchange::ETaskState::ProcessOk)
	{
		return false;
	}
	//Grab the result file and fill the BaseNodeContainer
	UE::Interchange::FJsonLoadSourceCmd::JsonResultParser ResultParser;
	ResultParser.FromJson(JsonResult);
	FString BaseNodeContainerFilename = ResultParser.GetResultFilename();

	//Parse the filename to fill the container
	BaseNodeContainer.LoadFromFile(BaseNodeContainerFilename);

	return true;
}

void UInterchangeFbxTranslator::ReleaseSource()
{
	if (Dispatcher.IsValid())
	{
		//Do not block the main thread
		Dispatcher->StopProcess(!IsInGameThread());
	}
}

void UInterchangeFbxTranslator::ImportFinish()
{
	if (Dispatcher.IsValid())
	{
		Dispatcher->TerminateProcess();
	}
}


TOptional<UE::Interchange::FImportImage> UInterchangeFbxTranslator::GetTexturePayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	UInterchangeSourceData* PayloadSourceData = UInterchangeManager::GetInterchangeManager().CreateSourceData(PayLoadKey);
	FGCObjectScopeGuard ScopedSourceData(PayloadSourceData);
	
	if (!PayloadSourceData)
	{
		return TOptional<UE::Interchange::FImportImage>();
	}
	UInterchangeTranslatorBase* SourceTranslator = UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData(PayloadSourceData);
	FGCObjectScopeGuard ScopedSourceTranslator(SourceTranslator);
	const IInterchangeTexturePayloadInterface* TextureTranslator = Cast<IInterchangeTexturePayloadInterface>(SourceTranslator);
	if (!ensure(TextureTranslator))
	{
		return TOptional<UE::Interchange::FImportImage>();
	}
	return TextureTranslator->GetTexturePayloadData(PayloadSourceData, PayLoadKey);
}

TOptional<UE::Interchange::FStaticMeshPayloadData> UInterchangeFbxTranslator::GetStaticMeshPayloadData(const FString& PayLoadKey) const
{
	//Not implemented, currently we do not have any payload data for static meshes
	return TOptional<UE::Interchange::FStaticMeshPayloadData>();
}

TFuture<TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>> UInterchangeFbxTranslator::GetSkeletalMeshLodPayloadData(const FString& PayLoadKey) const
{
	TSharedPtr<TPromise<TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>>> Promise = MakeShared<TPromise<TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>>>();
	if (!Dispatcher.IsValid())
	{
		Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>{});
		return Promise->GetFuture();
	}

	//Create a json command to read the fbx file
	const FString JsonCommand = CreateFetchPayloadFbxCommand(PayLoadKey);
	const int32 CreatedTaskIndex = Dispatcher->AddTask(JsonCommand, FInterchangeDispatcherTaskCompleted::CreateLambda([this, Promise](const int32 TaskIndex)
	{
		UE::Interchange::ETaskState TaskState;
		FString JsonResult;
		TArray<FString> JSonMessages;
		Dispatcher->GetTaskState(TaskIndex, TaskState, JsonResult, JSonMessages);

		//TODO: Parse the JSonMessage and add the message to the interchange not yet develop error messaging
		if (TaskState != UE::Interchange::ETaskState::ProcessOk)
		{
			Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>{});
			return;
		}
		//Grab the result file and fill the BaseNodeContainer
		UE::Interchange::FJsonFetchPayloadCmd::JsonResultParser ResultParser;
		ResultParser.FromJson(JsonResult);
		FString SkeletalMeshPayloadFilename = ResultParser.GetResultFilename();

		if (!ensure(FPaths::FileExists(SkeletalMeshPayloadFilename)))
		{
			//TODO log an error saying the payload file do not exist even if the get payload command succeed
			Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>{});
			return;
		}
		
		//All sub object should be gone with the reset
		TArray64<uint8> Buffer;
		FFileHelper::LoadFileToArray(Buffer, *SkeletalMeshPayloadFilename);
		uint8* FileData = Buffer.GetData();
		int64 FileDataSize = Buffer.Num();
		if (FileDataSize < 1)
		{
			//Nothing to load from this file
			Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>{});
			return;
		}

		UE::Interchange::FSkeletalMeshLodPayloadData SkeletalMeshLodPayload;
		SkeletalMeshLodPayload.LodMeshDescription.Empty();

		//Buffer keep the ownership of the data, the large memory reader is use to serialize the TMap
		FLargeMemoryReader Ar(FileData, FileDataSize);
		SkeletalMeshLodPayload.LodMeshDescription.Serialize(Ar);
		bool bFetchSkinnedData = false;
		Ar << bFetchSkinnedData;
		if (bFetchSkinnedData)
		{
			//Read the bone Name to remap the influence correctly
			Ar << SkeletalMeshLodPayload.JointNames;
		}
		Promise->SetValue(MoveTemp(SkeletalMeshLodPayload));
	}));

	//The task was not added to the dispatcher
	if (CreatedTaskIndex == INDEX_NONE)
	{
		Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshLodPayloadData>{});
	}

	return Promise->GetFuture();
}

TFuture<TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>> UInterchangeFbxTranslator::GetSkeletalMeshBlendShapePayloadData(const FString& PayLoadKey) const
{
	TSharedPtr<TPromise<TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>>> Promise = MakeShared<TPromise<TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>>>();
	if (!Dispatcher.IsValid())
	{
		Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>{});
		return Promise->GetFuture();
	}

	//Create a json command to read the fbx file
	const FString JsonCommand = CreateFetchPayloadFbxCommand(PayLoadKey);
	const int32 CreatedTaskIndex = Dispatcher->AddTask(JsonCommand, FInterchangeDispatcherTaskCompleted::CreateLambda([this, Promise](const int32 TaskIndex)
	{
		UE::Interchange::ETaskState TaskState;
		FString JsonResult;
		TArray<FString> JSonMessages;
		Dispatcher->GetTaskState(TaskIndex, TaskState, JsonResult, JSonMessages);

		//TODO: Parse the JSonMessage and add the message to the interchange not yet develop error messaging

		if (TaskState != UE::Interchange::ETaskState::ProcessOk)
		{
			Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>{});
			return;
		}
		//Grab the result file and fill the BaseNodeContainer
		UE::Interchange::FJsonFetchPayloadCmd::JsonResultParser ResultParser;
		ResultParser.FromJson(JsonResult);
		FString SkeletalMeshPayloadFilename = ResultParser.GetResultFilename();

		if (!ensure(FPaths::FileExists(SkeletalMeshPayloadFilename)))
		{
			//TODO log an error saying the payload file do not exist even if the get payload command succeed
			Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>{});
			return;
		}
		//All sub object should be gone with the reset
		TArray64<uint8> Buffer;
		FFileHelper::LoadFileToArray(Buffer, *SkeletalMeshPayloadFilename);
		uint8* FileData = Buffer.GetData();
		int64 FileDataSize = Buffer.Num();
		if (FileDataSize < 1)
		{
			//Nothing to load from this file
			Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>{});
			return;
		}
		UE::Interchange::FSkeletalMeshBlendShapePayloadData SkeletalMeshBlendShapePayload;
		SkeletalMeshBlendShapePayload.LodMeshDescription.Empty();
		//Buffer keep the ownership of the data, the large memory reader is use to serialize the TMap
		FLargeMemoryReader Ar(FileData, FileDataSize);
		SkeletalMeshBlendShapePayload.LodMeshDescription.Serialize(Ar);
		Promise->SetValue(MoveTemp(SkeletalMeshBlendShapePayload));
	}));

	//The task was not added to the dispatcher
	if (CreatedTaskIndex == INDEX_NONE)
	{
		Promise->SetValue(TOptional<UE::Interchange::FSkeletalMeshBlendShapePayloadData>{});
	}

	return Promise->GetFuture();
}

FString UInterchangeFbxTranslator::CreateLoadFbxFileCommand(const FString& FbxFilePath) const
{
	UE::Interchange::FJsonLoadSourceCmd LoadSourceCommand(TEXT("FBX"), FbxFilePath);
	return LoadSourceCommand.ToJson();
}

FString UInterchangeFbxTranslator::CreateFetchPayloadFbxCommand(const FString& FbxPayloadKey) const
{
	UE::Interchange::FJsonFetchPayloadCmd PayloadCommand(TEXT("FBX"), FbxPayloadKey);
	return PayloadCommand.ToJson();
}