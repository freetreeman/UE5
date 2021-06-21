// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/AsyncLoading2.h"
#include "IO/PackageStore.h"
#include "IO/IoDispatcher.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/UObjectMarks.h"

class FBufferWriter;

class FPackageStoreNameMapBuilder
{
public:
	void SetNameMapType(FMappedName::EType InNameMapType)
	{
		NameMapType = InNameMapType;
	}

	void AddName(const FName& Name)
	{
		const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
		const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
		NameMap.Add(DisplayIndex);
		int32 Index = NameMap.Num();
		NameIndices.Add(ComparisonIndex, Index);
	}

	void MarkNamesAsReferenced(const TArray<FName>& Names, TArray<int32>& OutNameIndices)
	{
		for (const FName& Name : Names)
		{
			const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
			const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
			int32& Index = NameIndices.FindOrAdd(ComparisonIndex);
			if (Index == 0)
			{
				NameMap.Add(DisplayIndex);
				Index = NameMap.Num();
			}

			OutNameIndices.Add(Index - 1);
		}
	}

	void MarkNameAsReferenced(const FName& Name)
	{
		const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
		const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
		int32& Index = NameIndices.FindOrAdd(ComparisonIndex);
		if (Index == 0)
		{
			NameMap.Add(DisplayIndex);
			Index = NameMap.Num();
		}
	}

	FMappedName MapName(const FName& Name) const
	{
		const FNameEntryId Id = Name.GetComparisonIndex();
		const int32* Index = NameIndices.Find(Id);
		check(Index);
		return FMappedName::Create(*Index - 1, Name.GetNumber(), NameMapType);
	}

	const TArray<FNameEntryId>& GetNameMap() const
	{
		return NameMap;
	}

	void Empty()
	{
		NameIndices.Empty();
		NameMap.Empty();
	}

private:
	TMap<FNameEntryId, int32> NameIndices;
	TArray<FNameEntryId> NameMap;
	FMappedName::EType NameMapType = FMappedName::EType::Package;
};

class FPackageStorePackage
{
public:
	FPackageId GetId() const
	{
		return Id;
	}

	uint32 GetLoadOrder() const
	{
		return LoadOrder;
	}

	uint64 GetHeaderSize() const
	{
		return HeaderSize;
	}

	uint64 GetImportMapSize() const
	{
		return ImportMapSize;
	}

	uint64 GetExportMapSize() const
	{
		return ExportMapSize;
	}

	uint64 GetExportBundleCount() const
	{
		return GraphData.ExportBundles.Num();
	}

	uint64 GetExportBundleEntriesSize() const
	{
		return ExportBundleEntriesSize;
	}
	
	uint64 GetGraphDataSize() const
	{
		return GraphDataSize;
	}

	uint64 GetNameCount() const
	{
		return NameMapBuilder.GetNameMap().Num();
	}

	uint64 GetNameMapSize() const
	{
		return NameMapSize;
	}

	const TArray<FPackageId>& GetImportedPackageIds() const
	{
		return ImportedPackageIds;
	}

	const TSet<FPackageId>& GetImportedRedirectedPackageIds() const
	{
		return ImportedRedirectedPackageIds;
	}

	void RedirectFrom(FName SourcePackageName)
	{
		SourceName = SourcePackageName;
	}

	void AddShaderMapHash(const FSHAHash& ShaderMapHash)
	{
		ShaderMapHashes.Add(ShaderMapHash);
	}

	const TSet<FSHAHash>& GetShaderMapHashes() const
	{
		return ShaderMapHashes;
	}

private:
	struct FExportGraphNode;

	struct FExternalDependency
	{
		int32 ImportIndex = -1;
		FExportBundleEntry::EExportCommandType ExportBundleCommandType;
		bool bIsConfirmedMissing = false;
	};

	struct FExportGraphNode
	{
		FExportBundleEntry BundleEntry;
		TArray<FExportGraphNode*> InternalDependencies;
		TArray<FExternalDependency> ExternalDependencies;
		int32 ExportBundleIndex = -1;
		int32 IncomingEdgeCount = 0;
		bool bIsPublic = false;
	};

	struct FExport
	{
		FString FullName;
		FName ObjectName;
		FPackageObjectIndex GlobalImportIndex;
		FPackageObjectIndex OuterIndex;
		FPackageObjectIndex ClassIndex;
		FPackageObjectIndex SuperIndex;
		FPackageObjectIndex TemplateIndex;
		EObjectFlags ObjectFlags = RF_NoFlags;
		uint64 CookedSerialOffset = uint64(-1);
		uint64 SerialOffset = uint64(-1);
		uint64 SerialSize = uint64(-1);
		bool bNotForClient = false;
		bool bNotForServer = false;
		bool bIsPublic = false;
		FExportGraphNode* Nodes[FExportBundleEntry::ExportCommandType_Count] = { nullptr };
	};

	struct FExportBundle
	{
		uint64 SerialOffset = uint64(-1);
		TArray<FExportBundleEntry> Entries;
	};

	struct FImport
	{
		FString FullName;
		FPackageId PackageId;
		FPackageObjectIndex GlobalImportIndex;
		bool bIsScriptImport = false;
		bool bIsPackageImport = false;
	};

	struct FInternalArc
	{
		int32 FromExportBundleIndex;
		int32 ToExportBundleIndex;

		bool operator==(const FInternalArc& Other) const
		{
			return FromExportBundleIndex == Other.FromExportBundleIndex &&
				ToExportBundleIndex == Other.ToExportBundleIndex;
		}
	};

	struct FExternalArc
	{
		int32 FromImportIndex;
		FExportBundleEntry::EExportCommandType FromCommandType;
		int32 ToExportBundleIndex;

		bool operator==(const FExternalArc& Other) const
		{
			return FromImportIndex == Other.FromImportIndex &&
				FromCommandType == Other.FromCommandType &&
				ToExportBundleIndex == Other.ToExportBundleIndex;
		}
	};

	struct FGraphData
	{
		TArray<FExportBundle> ExportBundles;
		TArray<FInternalArc> InternalArcs;
		TMap<FPackageId, TArray<FExternalArc>> ExternalArcs;
	};

	struct FExportBundleGraphNode
	{
		FPackageStorePackage* Package = nullptr;
		TArray<FExportGraphNode*> ExportGraphNodes;
		int32 Index = -1;
		int32 IncomingEdgeCount = 0;
	};

	FPackageId Id;
	FName Name;
	FName SourceName;
	FString Region;

	FPackageStoreNameMapBuilder NameMapBuilder;
	TArray<FImport> Imports;
	TArray<FExport> Exports;
	TArray<FExportGraphNode> ExportGraphNodes;
	FGraphData GraphData;

	TArray<FPackageId> ImportedPackageIds;
	TSet<FPackageId> RedirectedToPackageIds;
	TSet<FPackageId> ImportedRedirectedPackageIds;
	TSet<FSHAHash> ShaderMapHashes;

	FIoBuffer HeaderBuffer;

	uint32 PackageFlags = 0;
	uint32 CookedHeaderSize = 0;
	uint64 HeaderSize = 0;
	uint64 ExportsSerialSize = 0;
	uint64 ImportMapSize = 0;
	uint64 ExportMapSize = 0;
	uint64 ExportBundleEntriesSize = 0;
	uint64 GraphDataSize = 0;
	uint64 NameMapSize = 0;
	uint32 LoadOrder = 0;
	
	TArray<FExportBundleGraphNode> ExportBundleGraphNodes;
	FPackageStorePackage::FExportBundle* CurrentBundle = nullptr;
	TArray<FExportBundleGraphNode*> NodesWithNoIncomingEdges;
	bool bTemporaryMark = false;
	bool bPermanentMark = false;

	bool bIsRedirected = false;

	friend class FPackageStoreOptimizer;
};

class FPackageStoreOptimizer
{
public:
	uint64 GetTotalPackageCount() const
	{
		return TotalPackageCount;
	}

	uint64 GetTotalExportBundleCount() const
	{
		return TotalExportBundleCount;
	}

	uint64 GetTotalExportBundleEntryCount() const
	{
		return TotalExportBundleEntryCount;
	}

	uint64 GetTotalInternalBundleArcsCount() const
	{
		return TotalInternalBundleArcsCount;
	}

	uint64 GetTotalExternalBundleArcsCount() const
	{
		return TotalExternalBundleArcsCount;
	}

	uint64 GetTotalScriptObjectCount() const
	{
		return TotalScriptObjectCount;
	}

	void Initialize(const ITargetPlatform* TargetPlatform);

	FPackageStorePackage* CreateMissingPackage(const FName& Name) const;
	FPackageStorePackage* CreatePackageFromCookedHeader(const FName& Name, const FIoBuffer& CookedHeaderBuffer) const;
	FPackageStorePackage* CreatePackageFromPackageStoreHeader(const FName& Name, const FIoBuffer& Buffer, const FPackageStoreEntryResource& PackageStoreEntry) const;
	void FinalizePackage(FPackageStorePackage* Package);
	FIoBuffer CreatePackageBuffer(const FPackageStorePackage* Package, const FIoBuffer& CookedExportsBuffer, TArray<FFileRegion>* InOutFileRegions) const;
	FPackageStoreEntryResource CreatePackageStoreEntry(const FPackageStorePackage* Package) const;
	FContainerHeader CreateContainerHeader(const FIoContainerId& ContainerId, TArrayView<const FPackageStoreEntryResource> PackageStoreEntries) const;
	FIoBuffer CreateScriptObjectsBuffer() const;
	void ProcessRedirects(const TMap<FPackageId, FPackageStorePackage*>& PackagesMap) const;
	void OptimizeExportBundles(const TMap<FPackageId, FPackageStorePackage*>& PackagesMap);
	void FindImports(const TMap<FPackageId, FPackageStorePackage*>& PackagesMap) const;

private:
	struct FScriptObjectData
	{
		FName ObjectName;
		FString FullName;
		FPackageObjectIndex GlobalIndex;
		FPackageObjectIndex OuterIndex;
		FPackageObjectIndex CDOClassIndex;
	};

	struct FCookedHeaderData
	{
		FPackageFileSummary Summary;
		TArray<FName> SummaryNames;
		TArray<FObjectImport> ObjectImports;
		TArray<FObjectExport> ObjectExports;
		TArray<FPackageIndex> PreloadDependencies;
	};

	struct FPackageStoreHeaderData
	{
		FPackageSummary Summary;
		TArray<FPackageId> ImportedPackageIds;
		TArray<FNameEntryId> NameMap;
		TArray<FPackageObjectIndex> Imports;
		TArray<FExportMapEntry> Exports;
		TArray<FExportBundleHeader> ExportBundleHeaders;
		TArray<FExportBundleEntry> ExportBundleEntries;
		TArray<FPackageStorePackage::FInternalArc> InternalArcs;
		TArray<FPackageStorePackage::FExternalArc> ExternalArcs;
	};

	using FExportGraphEdges = TMultiMap<FPackageStorePackage::FExportGraphNode*, FPackageStorePackage::FExportGraphNode*>;
	using FExportBundleGraphEdges = TMultiMap<FPackageStorePackage::FExportBundleGraphNode*, FPackageStorePackage::FExportBundleGraphNode*>;

	FCookedHeaderData LoadCookedHeader(const FIoBuffer& CookedHeaderBuffer) const;
	FPackageStoreHeaderData LoadPackageStoreHeader(const FIoBuffer& PackageStoreHeaderBuffer, const FPackageStoreEntryResource& PackageStoreEntry) const;
	void ResolveImport(FPackageStorePackage::FImport* Imports, const FObjectImport* ObjectImports, int32 LocalImportIndex) const;
	void ProcessImports(const FCookedHeaderData& CookedHeaderData, FPackageStorePackage* Package) const;
	void ProcessImports(const FPackageStoreHeaderData& PackageStoreHeaderData, FPackageStorePackage* Package) const;
	void ResolveExport(FPackageStorePackage::FExport* Exports, const FObjectExport* ObjectExports, const int32 LocalExportIndex, const FName& PackageName) const;
	void ResolveExport(FPackageStorePackage::FExport* Exports, const int32 LocalExportIndex, const FName& PackageName) const;
	void ProcessExports(const FCookedHeaderData& CookedHeaderData, FPackageStorePackage* Package) const;
	void ProcessExports(const FPackageStoreHeaderData& PackageStoreHeaderData, FPackageStorePackage* Package) const;
	void ProcessPreloadDependencies(const FCookedHeaderData& CookedHeaderData, FPackageStorePackage* Package) const;
	void ProcessPreloadDependencies(const FPackageStoreHeaderData& PackageStoreHeaderData, FPackageStorePackage* Package) const;
	TArray<FPackageStorePackage*> SortPackagesInLoadOrder(const TMap<FPackageId, FPackageStorePackage*>& PackagesMap) const;
	void SerializeGraphData(const TArray<FPackageId>& ImportedPackageIds, FPackageStorePackage::FGraphData& GraphData, FBufferWriter& GraphArchive) const;
	TArray<FPackageStorePackage::FExportGraphNode*> SortExportGraphNodesInLoadOrder(FPackageStorePackage* Package, FExportGraphEdges& Edges) const;
	TArray<FPackageStorePackage::FExportBundleGraphNode*> SortExportBundleGraphNodesInLoadOrder(const TArray<FPackageStorePackage*>& Packages, FExportBundleGraphEdges& Edges) const;
	void CreateExportBundles(FPackageStorePackage* Package) const;
	bool RedirectPackage(const FPackageStorePackage& SourcePackage, FPackageStorePackage& TargetPackage, TMap<FPackageObjectIndex, FPackageObjectIndex>& RedirectedToSourceImportIndexMap) const;
	void RedirectPackageUnverified(FPackageStorePackage& TargetPackage, TMap<FPackageObjectIndex, FPackageObjectIndex>& RedirectedToSourceImportIndexMap) const;
	void FinalizePackageHeader(FPackageStorePackage* Package) const;
	void FindScriptObjectsRecursive(FPackageObjectIndex OuterIndex, UObject* Object, EObjectMark ExcludedObjectMarks, const ITargetPlatform* TargetPlatform);
	void FindScriptObjects(const ITargetPlatform* TargetPlatform);

	TMap<FPackageObjectIndex, FScriptObjectData> ScriptObjectsMap;
	uint64 TotalPackageCount = 0;
	uint64 TotalExportBundleCount = 0;
	uint64 TotalExportBundleEntryCount = 0;
	uint64 TotalInternalBundleArcsCount = 0;
	uint64 TotalExternalBundleArcsCount = 0;
	uint64 TotalScriptObjectCount = 0;
	uint32 NextLoadOrder = 0;
};
