// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/AsyncLoading2.h"
#include "IO/IoDispatcher.h"
#include "Containers/Map.h"
#include "Serialization/CompactBinary.h"
#include "IO/PackageStore.h"
#include "IO/PackageStoreWriter.h"
#include "FilePackageStoreWriter.h"

namespace UE {
	class FZenStoreHttpClient;
}

class FPackageStoreOptimizer;
class FZenFileSystemManifest;
class FCbPackage;
class FCbWriter;

/** 
 * Client for interfacing with Zen storage service
 */

class FZenStoreWriter
	: public IPackageStoreWriter
{
public:
	IOSTOREUTILITIES_API FZenStoreWriter(	const FString& OutputPath, 
											const FString& MetadataDirectoryPath, 
											const ITargetPlatform* TargetPlatform, 
											bool IsCleanBuild);

	IOSTOREUTILITIES_API ~FZenStoreWriter();

	IOSTOREUTILITIES_API virtual void BeginPackage(const FPackageBaseInfo& Info) override;
	IOSTOREUTILITIES_API virtual void CommitPackage(const FPackageBaseInfo& Info) override;

	IOSTOREUTILITIES_API virtual void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) override;
	IOSTOREUTILITIES_API virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API virtual void BeginCook(const FCookInfo& Info) override;
	IOSTOREUTILITIES_API virtual void EndCook() override;

	IOSTOREUTILITIES_API virtual void GetEntries(TFunction<void(TArrayView<const FPackageStoreEntryResource>)>&& Callback) override;

	DECLARE_DERIVED_EVENT(FZenStoreWriter, IPackageStoreWriter::FCommitEvent, FCommitEvent);
	IOSTOREUTILITIES_API virtual FCommitEvent& OnCommit() override
	{
		return CommitEvent;
	}

	virtual void Flush() override;

	IOSTOREUTILITIES_API void WriteIoStorePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const FPackageStoreEntryResource& PackageStoreEntry, const TArray<FFileRegion>& FileRegions);

private:
	void CreateProjectMetaData(FCbPackage& Pkg, FCbWriter& PackageObj, bool bGenerateContainerHeader);
	void BroadcastCommit(IPackageStoreWriter::FCommitEventArgs& EventArgs);

	struct FBulkDataEntry
	{
		FIoBuffer Payload;
		FBulkDataInfo Info;
		FCbObjectId ChunkId;
		bool IsValid = false;
	};

	struct FPackageDataEntry
	{
		FIoBuffer Payload;
		FPackageInfo Info;
		FCbObjectId ChunkId;
		FPackageStoreEntryResource PackageStoreEntry;
		bool IsValid = false;
	};

	struct FFileDataEntry
	{
		FIoBuffer Payload;
		FAdditionalFileInfo Info;
		FString ZenManifestServerPath;
		FString ZenManifestClientPath;
	};

	struct FPendingPackageState
	{
		FName PackageName;
		FPackageDataEntry PackageData;
		TArray<FBulkDataEntry> BulkData;
		TArray<FFileDataEntry> FileData;
	};

	struct FZenStats
	{
		uint64 TotalBytes = 0;
		double TotalRequestTime = 0.0;
	};

	FRWLock								PackagesLock;
	TMap<FName,FPendingPackageState>	PendingPackages;
	TUniquePtr<UE::FZenStoreHttpClient>	HttpClient;

	const ITargetPlatform&				TargetPlatform;
	FString								OutputPath;
	FString								MetadataDirectoryPath;
	FIoContainerId						ContainerId = FIoContainerId::FromName(TEXT("global"));

	FPackageStoreManifest				PackageStoreManifest;
	TUniquePtr<FPackageStoreOptimizer>	PackageStoreOptimizer;
	TArray<FPackageStoreEntryResource>	PackageStoreEntries;
	TUniquePtr<FZenFileSystemManifest>	ZenFileSystemManifest;
	
	FCriticalSection					CommitEventCriticalSection;
	FCommitEvent						CommitEvent;

	class FZenStoreHttpQueue;
	TUniquePtr<FZenStoreHttpQueue>		HttpQueue;
	
	IPackageStoreWriter::FCookInfo::ECookMode CookMode;

	FZenStats							ZenStats;
};
