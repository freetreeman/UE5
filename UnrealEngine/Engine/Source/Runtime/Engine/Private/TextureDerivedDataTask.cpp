// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TextureDerivedDataTask.cpp: Tasks to update texture DDC.
=============================================================================*/

#include "TextureDerivedDataTask.h"
#include "Misc/CommandLine.h"
#include "Stats/Stats.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Serialization/MemoryReader.h"
#include "UObject/Package.h"
#include "RenderUtils.h"
#include "TextureResource.h"
#include "Engine/Texture.h"
#include "Engine/Texture2DArray.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "VT/VirtualTextureBuiltData.h"

#if WITH_EDITOR

#include "DerivedDataBuild.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildInputResolver.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildSession.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataPayload.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITextureFormat.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FeedbackContext.h"
#include "ProfilingDebugging/CookStats.h"
#include "TextureDerivedDataBuildUtils.h"
#include "VT/VirtualTextureDataBuilder.h"

static TAutoConsoleVariable<int32> CVarVTValidateCompressionOnLoad(
	TEXT("r.VT.ValidateCompressionOnLoad"),
	0,
	TEXT("Validates that VT data contains no compression errors when loading from DDC")
	TEXT("This is slow, but allows debugging corrupt VT data (and allows recovering from bad DDC)")
);

static TAutoConsoleVariable<int32> CVarVTValidateCompressionOnSave(
	TEXT("r.VT.ValidateCompressionOnSave"),
	0,
	TEXT("Validates that VT data contains no compression errors before saving to DDC")
	TEXT("This is slow, but allows debugging corrupt VT data")
);

void GetTextureDerivedDataKeyFromSuffix(const FString& KeySuffix, FString& OutKey);

class FTextureStatusMessageContext : public FScopedSlowTask
{
public:
	explicit FTextureStatusMessageContext(const FText& InMessage)
		: FScopedSlowTask(0, InMessage, IsInGameThread())
	{
		UE_LOG(LogTexture,Display,TEXT("%s"),*InMessage.ToString());
	}
};

static bool ValidateTexture2DPlatformData(const FTexturePlatformData& TextureData, const UTexture2D& Texture, bool bFromDDC)
{
	// Temporarily disable as the size check reports false negatives on some platforms
#if 0
	bool bValid = true;
	for (int32 MipIndex = 0; MipIndex < TextureData.Mips.Num(); ++MipIndex)
	{
		const FTexture2DMipMap& MipMap = TextureData.Mips[MipIndex];
		const int64 BulkDataSize = MipMap.BulkData.GetBulkDataSize();
		if (BulkDataSize > 0)
		{
			const int64 ExpectedMipSize = CalcTextureMipMapSize(TextureData.SizeX, TextureData.SizeY, TextureData.PixelFormat, MipIndex);
			if (BulkDataSize != ExpectedMipSize)
			{
				//UE_LOG(LogTexture,Warning,TEXT("Invalid mip data. Texture will be rebuilt. MipIndex %d [%dx%d], Expected size %lld, BulkData size %lld, PixelFormat %s, LoadedFromDDC %d, Texture %s"), 
				//	MipIndex, 
				//	MipMap.SizeX, 
				//	MipMap.SizeY, 
				//	ExpectedMipSize, 
				//	BulkDataSize, 
				//	GPixelFormats[TextureData.PixelFormat].Name, 
				//	bFromDDC ? 1 : 0,
				//	*Texture.GetFullName());
				
				bValid = false;
			}
		}
	}

	return bValid;
#else
	return true;
#endif
}

void FTextureSourceData::Init(UTexture& InTexture, const FTextureBuildSettings* InBuildSettingsPerLayer, bool bAllowAsyncLoading)
{
	const int32 NumBlocks = InTexture.Source.GetNumBlocks();
	const int32 NumLayers = InTexture.Source.GetNumLayers();
	if (NumBlocks < 1 || NumLayers < 1)
	{
		UE_LOG(LogTexture, Warning, TEXT("Texture has no source data: %s"), *InTexture.GetPathName());
		return;
	}

	Layers.Reserve(NumLayers);
	for (int LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		FTextureSourceLayerData* LayerData = new(Layers) FTextureSourceLayerData();
		switch (InTexture.Source.GetFormat(LayerIndex))
		{
		case TSF_G8:		LayerData->ImageFormat = ERawImageFormat::G8;		break;
		case TSF_G16:		LayerData->ImageFormat = ERawImageFormat::G16;		break;
		case TSF_BGRA8:		LayerData->ImageFormat = ERawImageFormat::BGRA8;	break;
		case TSF_BGRE8:		LayerData->ImageFormat = ERawImageFormat::BGRE8;	break;
		case TSF_RGBA16:	LayerData->ImageFormat = ERawImageFormat::RGBA16;	break;
		case TSF_RGBA16F:	LayerData->ImageFormat = ERawImageFormat::RGBA16F;  break;
		default:
			UE_LOG(LogTexture, Fatal, TEXT("Texture %s has source art in an invalid format."), *InTexture.GetName());
			return;
		}

		FTextureFormatSettings FormatSettings;
		InTexture.GetLayerFormatSettings(LayerIndex, FormatSettings);
		LayerData->GammaSpace = FormatSettings.SRGB ? (InTexture.bUseLegacyGamma ? EGammaSpace::Pow22 : EGammaSpace::sRGB) : EGammaSpace::Linear;
	}

	Blocks.Reserve(NumBlocks);
	for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
	{
		FTextureSourceBlock SourceBlock;
		InTexture.Source.GetBlock(BlockIndex, SourceBlock);

		if (SourceBlock.NumMips > 0 && SourceBlock.NumSlices > 0)
		{
			FTextureSourceBlockData* BlockData = new(Blocks) FTextureSourceBlockData();
			BlockData->BlockX = SourceBlock.BlockX;
			BlockData->BlockY = SourceBlock.BlockY;
			BlockData->SizeX = SourceBlock.SizeX;
			BlockData->SizeY = SourceBlock.SizeY;
			BlockData->NumMips = SourceBlock.NumMips;
			BlockData->NumSlices = SourceBlock.NumSlices;

			if (InBuildSettingsPerLayer[0].MipGenSettings != TMGS_LeaveExistingMips)
			{
				BlockData->NumMips = 1;
			}

			if (!InBuildSettingsPerLayer[0].bCubemap && !InBuildSettingsPerLayer[0].bTextureArray && !InBuildSettingsPerLayer[0].bVolume)
			{
				BlockData->NumSlices = 1;
			}

			BlockData->MipsPerLayer.SetNum(NumLayers);

			SizeInBlocksX = FMath::Max(SizeInBlocksX, SourceBlock.BlockX + 1);
			SizeInBlocksY = FMath::Max(SizeInBlocksY, SourceBlock.BlockY + 1);
			BlockSizeX = FMath::Max(BlockSizeX, SourceBlock.SizeX);
			BlockSizeY = FMath::Max(BlockSizeY, SourceBlock.SizeY);
		}
	}

	for (FTextureSourceBlockData& Block : Blocks)
	{
		const int32 MipBiasX = FMath::CeilLogTwo(BlockSizeX / Block.SizeX);
		const int32 MipBiasY = FMath::CeilLogTwo(BlockSizeY / Block.SizeY);
		if (MipBiasX != MipBiasY)
		{
			UE_LOG(LogTexture, Warning, TEXT("Texture has blocks with mismatched aspect ratios"), *InTexture.GetPathName());
			return;
		}

		Block.MipBias = MipBiasX;
	}

	TextureName = InTexture.GetFName();

	if (bAllowAsyncLoading && !InTexture.Source.IsBulkDataLoaded())
	{
		// Prepare the async source to be later able to load it from file if required.
		AsyncSource = InTexture.Source; // This copies information required to make a safe IO load async.
	}

	bValid = true;
}

void FTextureSourceData::GetSourceMips(FTextureSource& Source, IImageWrapperModule* InImageWrapper)
{
	if (bValid)
	{
		if (Source.HasHadBulkDataCleared())
		{	// don't do any work we can't reload this
			UE_LOG(LogTexture, Error, TEXT("Unable to get texture source mips because its bulk data was released. %s"), *TextureName.ToString())
				return;
		}

		const FTextureSource::FMipData ScopedMipData = Source.GetMipData(InImageWrapper);

		for (int32 BlockIndex = 0; BlockIndex < Blocks.Num(); ++BlockIndex)
		{
			FTextureSourceBlock SourceBlock;
			Source.GetBlock(BlockIndex, SourceBlock);

			FTextureSourceBlockData& BlockData = Blocks[BlockIndex];
			for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
			{
				const FTextureSourceLayerData& LayerData = Layers[LayerIndex];
				if (!BlockData.MipsPerLayer[LayerIndex].Num()) // If we already got valid data, nothing to do.
				{
					int32 MipSizeX = SourceBlock.SizeX;
					int32 MipSizeY = SourceBlock.SizeY;
					for (int32 MipIndex = 0; MipIndex < BlockData.NumMips; ++MipIndex)
					{
						FImage* SourceMip = new(BlockData.MipsPerLayer[LayerIndex]) FImage(
							MipSizeX, MipSizeY,
							BlockData.NumSlices,
							LayerData.ImageFormat,
							LayerData.GammaSpace
						);

						if (!ScopedMipData.GetMipData(SourceMip->RawData, BlockIndex, LayerIndex, MipIndex))
						{
							UE_LOG(LogTexture, Warning, TEXT("Cannot retrieve source data for mip %d of texture %s"), MipIndex, *TextureName.ToString());
							ReleaseMemory();
							bValid = false;
							break;
						}

						MipSizeX = FMath::Max(MipSizeX / 2, 1);
						MipSizeY = FMath::Max(MipSizeY / 2, 1);
					}
				}
			}
		}
	}
}

void FTextureSourceData::GetAsyncSourceMips(IImageWrapperModule* InImageWrapper)
{
	if (bValid && !Blocks[0].MipsPerLayer[0].Num() && AsyncSource.GetSizeOnDisk())
	{
		if (AsyncSource.LoadBulkDataWithFileReader())
		{
			GetSourceMips(AsyncSource, InImageWrapper);
		}
	}
}
void FTextureCacheDerivedDataWorker::ConsumeBuildFunctionOutput(const UE::DerivedData::FBuildOutput& BuildOutput, const FString& TexturePath, bool bReplaceExistingDDC)
{
	using namespace UE::DerivedData;

	FSharedBuffer PrimaryData;
	{
		FPayloadId PrimaryPayloadId = FPayloadId::FromName(TEXT("Texture"));
		const FPayload& PrimaryPayload = BuildOutput.GetPayload(PrimaryPayloadId);
		if (PrimaryPayload.IsNull())
		{
			UE_LOG(LogTexture, Warning, TEXT("Texture build function missing primary output payload when building %s derived data for %s"), *BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *Texture.GetPathName());
			return;
		}
		PrimaryData = PrimaryPayload.GetData().Decompress();
		TArrayView<const uint8> PrimaryDataView((const uint8*)PrimaryData.GetData(), PrimaryData.GetSize());

		FMemoryReaderView Ar(PrimaryDataView, /*bIsPersistent=*/ true);
		DerivedData->Serialize(Ar, NULL);
	}

	if (DerivedData->Mips.Num())
	{
		const bool bInlineMips = (CacheFlags & ETextureCacheFlags::InlineMips) != 0;

		for (int32 MipIndex = 0; MipIndex < DerivedData->Mips.Num(); ++MipIndex)
		{
			FTexture2DMipMap& Mip = DerivedData->Mips[MipIndex];
			if (Mip.DerivedDataKey.IsEmpty())
			{
				break;
			}

			TStringBuilder<32> PayloadName;
			PayloadName << "Mip" << MipIndex;

			FPayloadId MipPayloadId = FPayloadId::FromName(*PayloadName);
			const FPayload& MipPayload = BuildOutput.GetPayload(MipPayloadId);
			if (MipPayload.IsNull())
			{
				UE_LOG(LogTexture, Warning, TEXT("Texture build function missing Mip%d output payload when building %s derived data for %s"), MipIndex, *BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *Texture.GetPathName());
				return;
			}
			FSharedBuffer MipData = MipPayload.GetData().Decompress();
			TArrayView<const uint8> MipDataView((const uint8*)MipData.GetData(), MipData.GetSize());
			GetDerivedDataCacheRef().Put(*Mip.DerivedDataKey, MipDataView, TexturePath, bReplaceExistingDDC);

			if (bInlineMips && (MipIndex >= (int32)BuildSettingsPerLayer[0].LODBiasWithCinematicMips))
			{
				int32 MipSize = 0;
				FMemoryReaderView Ar(MipDataView, /*bIsPersistent=*/ true);
				Ar << MipSize;

				Mip.BulkData.Lock(LOCK_READ_WRITE);
				void* MipAllocData = Mip.BulkData.Realloc(MipSize);
				Ar.Serialize(MipAllocData, MipSize);
				Mip.BulkData.Unlock();
				Mip.DerivedDataKey.Empty();
			}
		}

		FString DerivedDataKey;
		GetTextureDerivedDataKeyFromSuffix(KeySuffix, DerivedDataKey);
		TArrayView<const uint8> PrimaryDataView((const uint8*)PrimaryData.GetData(), PrimaryData.GetSize());
		GetDerivedDataCacheRef().Put(*DerivedDataKey, PrimaryDataView, TexturePath, bReplaceExistingDDC);

		bSucceeded = true;
	}
	else
	{
		UE_LOG(LogTexture, Warning, TEXT("Failed to build %s derived data for %s"), *BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *Texture.GetPathName());
	}
}

void FTextureCacheDerivedDataWorker::BuildTexture(bool bReplaceExistingDDC)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCacheDerivedDataWorker::BuildTexture);

	const bool bHasValidMip0 = TextureData.Blocks.Num() && TextureData.Blocks[0].MipsPerLayer.Num() && TextureData.Blocks[0].MipsPerLayer[0].Num();
	const bool bForVirtualTextureStreamingBuild = (CacheFlags & ETextureCacheFlags::ForVirtualTextureStreamingBuild) != 0;	

	FFormatNamedArguments Args;
	Args.Add(TEXT("TextureName"), FText::FromString(Texture.GetName()));
	Args.Add(TEXT("TextureFormatName"), FText::FromString(BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString()));
	Args.Add(TEXT("IsVT"), FText::FromString( FString( bForVirtualTextureStreamingBuild ? TEXT(" VT") : TEXT("") ) ) );
	Args.Add(TEXT("TextureResolutionX"), FText::FromString(FString::FromInt(bHasValidMip0 ? TextureData.Blocks[0].MipsPerLayer[0][0].SizeX : 0)));
	Args.Add(TEXT("TextureResolutionY"), FText::FromString(FString::FromInt(bHasValidMip0 ? TextureData.Blocks[0].MipsPerLayer[0][0].SizeY : 0)));
	Args.Add(TEXT("NumBlocks"), FText::FromString(FString::FromInt(TextureData.Blocks.Num())));
	Args.Add(TEXT("NumLayers"), FText::FromString(FString::FromInt(TextureData.Layers.Num())));
	Args.Add(TEXT("EstimatedMemory"), FText::FromString(FString::SanitizeFloat(double(RequiredMemoryEstimate) / (1024.0*1024.0), 3)));

	FTextureStatusMessageContext StatusMessage(
		FText::Format(
			NSLOCTEXT("Engine", "BuildTextureStatus", "Building textures: {TextureName} ({TextureFormatName}{IsVT}, {TextureResolutionX}X{TextureResolutionY} X{NumBlocks}X{NumLayers}) (Required Memory Estimate: {EstimatedMemory} MB)"), 
			Args
		));

	if (!ensure(Compressor))
	{
		UE_LOG(LogTexture, Warning, TEXT("Missing Compressor required to build texture %s"), *Texture.GetPathName());
		return;
	}

	if (!bHasValidMip0)
	{
		return;
	}

	if (bForVirtualTextureStreamingBuild)
	{
		if (DerivedData->VTData == nullptr)
		{
			DerivedData->VTData = new FVirtualTextureBuiltData();
		}

		FVirtualTextureDataBuilder Builder(*DerivedData->VTData, Compressor, ImageWrapper);
		Builder.Build(TextureData, CompositeTextureData, &BuildSettingsPerLayer[0], true);

		DerivedData->SizeX = DerivedData->VTData->Width;
		DerivedData->SizeY = DerivedData->VTData->Height;
		DerivedData->PixelFormat = DerivedData->VTData->LayerTypes[0];
		DerivedData->SetNumSlices(1);

		bool bCompressionValid = true;
		if (CVarVTValidateCompressionOnSave.GetValueOnAnyThread())
		{
			bCompressionValid = DerivedData->VTData->ValidateData(Texture.GetPathName(), true);
		}

		if (ensureMsgf(bCompressionValid, TEXT("Corrupt Virtual Texture compression for %s, can't store to DDC"), *Texture.GetPathName()))
		{
			// Store it in the cache.
			// @todo: This will remove the streaming bulk data, which we immediately reload below!
			// Should ideally avoid this redundant work, but it only happens when we actually have 
			// to build the texture, which should only ever be once.
			this->BytesCached = PutDerivedDataInCache(DerivedData, KeySuffix, Texture.GetPathName(), BuildSettingsPerLayer[0].bCubemap || BuildSettingsPerLayer[0].bVolume || BuildSettingsPerLayer[0].bTextureArray, bReplaceExistingDDC);

			if (DerivedData->VTData->Chunks.Num())
			{
				const bool bInlineMips = (CacheFlags & ETextureCacheFlags::InlineMips) != 0;
				bSucceeded = !bInlineMips || DerivedData->TryInlineMipData(BuildSettingsPerLayer[0].LODBiasWithCinematicMips, &Texture);
				if (!bSucceeded)
				{
					UE_LOG(LogTexture, Display, TEXT("Failed to put and then read back mipmap data from DDC for %s"), *Texture.GetPathName());
				}
			}
			else
			{
				UE_LOG(LogTexture, Warning, TEXT("Failed to build %s derived data for %s"), *BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *Texture.GetPathName());
			}
		}
	}
	else
	{
		// Only support single Block/Layer here (Blocks and Layers are intended for VT support)
		if (TextureData.Blocks.Num() > 1)
		{
			// This warning can happen if user attempts to import a UDIM without VT enabled
			UE_LOG(LogTexture, Warning, TEXT("Texture %s was imported as UDIM with %d blocks but VirtualTexturing is not enabled, only the first block will be available"),
				*Texture.GetPathName(), TextureData.Blocks.Num());
		}

		// No user-facing way to generated multi-layered textures currently, so this should not occur
		if (TextureData.Layers.Num() > 1)
		{
			UE_LOG(LogTexture, Warning, TEXT("Texture %s has %d layers but VirtualTexturing is not enabled, only the first layer will be available"),
				*Texture.GetPathName(), TextureData.Layers.Num());
		}

		check(DerivedData->Mips.Num() == 0);
		DerivedData->SizeX = 0;
		DerivedData->SizeY = 0;
		DerivedData->PixelFormat = PF_Unknown;
		DerivedData->SetIsCubemap(false);
		DerivedData->VTData = nullptr;

		FOptTexturePlatformData OptData;

		BuildExporter.ExportTextureBuild(Texture, BuildSettingsPerLayer[0], 0, NUM_INLINE_DERIVED_MIPS);

		if (!BuildFunctionName.IsEmpty())
		{
			using namespace UE::DerivedData;
			// Compress the texture by calling the DDC2 build function.  May be executed locally or remotely.
			// The FBuildDefinition in the future will replace both the cache get/put as well as execution.
			IBuild& Build = GetDerivedDataBuildRef();
			FString TexturePath = Texture.GetPathName();
			FBuildDefinitionBuilder DefinitionBuilder = Build.CreateDefinition(TexturePath, BuildFunctionName);

			DefinitionBuilder.AddConstant(TEXT("Settings"_SV),
				SaveTextureBuildSettings(KeySuffix, Texture, BuildSettingsPerLayer[0], 0, NUM_INLINE_DERIVED_MIPS));
			DefinitionBuilder.AddInputBulkData(TEXT("Source"_SV), Texture.Source.GetId());
			if (Texture.CompositeTexture)
			{
				DefinitionBuilder.AddInputBulkData(TEXT("CompositeSource"_SV), Texture.CompositeTexture->Source.GetId());
			}

			class FTextureBuildInputResolver final : public IBuildInputResolver
			{
			public:
				explicit FTextureBuildInputResolver(UTexture& InTexture)
					: Texture(InTexture)
				{
				}

				const FCompressedBuffer& FindSource(FCompressedBuffer& Buffer, FTextureSource& Source, const FGuid& BulkDataId)
				{
					if (Source.GetId() != BulkDataId)
					{
						return FCompressedBuffer::Null;
					}
					if (!Buffer)
					{
						Source.OperateOnLoadedBulkData([&Buffer](const FSharedBuffer& BulkDataBuffer)
						{
							Buffer = FCompressedBuffer::Compress(NAME_Default, BulkDataBuffer);
						});
					}
					return Buffer;
				}

				FRequest ResolveInputMeta(
					const FBuildDefinition& Definition,
					EPriority Priority,
					FOnBuildInputMetaResolved&& OnResolved) final
				{
					EStatus Status = EStatus::Ok;
					TArray<FString> InputKeys;
					TArray<FBuildInputMetaByKey> Inputs;
					Definition.IterateInputBulkData([this, &Status, &InputKeys, &Inputs](FStringView Key, const FGuid& BulkDataId)
					{
						const FCompressedBuffer& Buffer = Key == TEXT("Source"_SV)
							? FindSource(SourceBuffer, Texture.Source, BulkDataId)
							: FindSource(CompositeSourceBuffer, Texture.CompositeTexture->Source, BulkDataId);
						if (Buffer)
						{
							InputKeys.Emplace(Key);
							Inputs.Add({InputKeys.Last(), Buffer.GetRawHash(), Buffer.GetRawSize()});
						}
						else
						{
							Status = EStatus::Error;
						}
					});
					OnResolved({Inputs, Status});
					return FRequest();
				}

				FRequest ResolveInputData(
					const FBuildDefinition& Definition,
					EPriority Priority,
					FOnBuildInputDataResolved&& OnResolved,
					FBuildInputFilter&& Filter) final
				{
					EStatus Status = EStatus::Ok;
					TArray<FString> InputKeys;
					TArray<FBuildInputDataByKey> Inputs;
					Definition.IterateInputBulkData([this, &Filter, &Status, &InputKeys, &Inputs](FStringView Key, const FGuid& BulkDataId)
					{
						if (!Filter || Filter(Key))
						{
							const FCompressedBuffer& Buffer = Key == TEXT("Source"_SV)
								? FindSource(SourceBuffer, Texture.Source, BulkDataId)
								: FindSource(CompositeSourceBuffer, Texture.CompositeTexture->Source, BulkDataId);
							if (Buffer)
							{
								InputKeys.Emplace(Key);
								Inputs.Add({InputKeys.Last(), Buffer});
							}
							else
							{
								Status = EStatus::Error;
							}
						}
					});
					OnResolved({Inputs, Status});
					return FRequest();
				}

			private:
				UTexture& Texture;
				FCompressedBuffer SourceBuffer;
				FCompressedBuffer CompositeSourceBuffer;
			};

			FTextureBuildInputResolver InputResolver(Texture);
			FBuildSession Session = Build.CreateSession(TexturePath, &InputResolver);
			Session.Build(DefinitionBuilder.Build(), EBuildPolicy::Default, EPriority::Blocking,
				[this, &TexturePath, bReplaceExistingDDC] (FBuildCompleteParams&& Params)
				{
					#if !NO_LOGGING
					Params.Output.IterateDiagnostics([this, &TexturePath] (const FBuildDiagnostic& Diagnostic)
						{
							if (GWarn)
							{
								FName CategoryName(Diagnostic.Category);
								GWarn->Log(CategoryName, Diagnostic.Level == EBuildDiagnosticLevel::Error ? ELogVerbosity::Error : ELogVerbosity::Warning, *FString(Diagnostic.Message));
							}
						});
					#endif

					if (Params.Status == EStatus::Ok)
					{
						ConsumeBuildFunctionOutput(Params.Output, TexturePath, bReplaceExistingDDC);
					}
				}).Wait();
		}
		else
		{
			// Compress the texture by calling texture compressor directly.
			TArray<FCompressedImage2D> CompressedMips;
			if (Compressor->BuildTexture(TextureData.Blocks[0].MipsPerLayer[0],
				((bool)Texture.CompositeTexture && CompositeTextureData.Blocks.Num() && CompositeTextureData.Blocks[0].MipsPerLayer.Num()) ? CompositeTextureData.Blocks[0].MipsPerLayer[0] : TArray<FImage>(),
				BuildSettingsPerLayer[0],
				CompressedMips,
				OptData.NumMipsInTail,
				OptData.ExtData))
			{
				check(CompressedMips.Num());

				// Build the derived data.
				const int32 MipCount = CompressedMips.Num();
				for (int32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
				{
					const FCompressedImage2D& CompressedImage = CompressedMips[MipIndex];
					FTexture2DMipMap* NewMip = new FTexture2DMipMap();
					DerivedData->Mips.Add(NewMip);
					NewMip->SizeX = CompressedImage.SizeX;
					NewMip->SizeY = CompressedImage.SizeY;
					NewMip->SizeZ = CompressedImage.SizeZ;
					NewMip->FileRegionType = FFileRegion::SelectType(EPixelFormat(CompressedImage.PixelFormat));
					check(NewMip->SizeZ == 1 || BuildSettingsPerLayer[0].bVolume || BuildSettingsPerLayer[0].bTextureArray); // Only volume & arrays can have SizeZ != 1
					NewMip->BulkData.Lock(LOCK_READ_WRITE);
					check(CompressedImage.RawData.GetTypeSize() == 1);
					void* NewMipData = NewMip->BulkData.Realloc(CompressedImage.RawData.Num());
					FMemory::Memcpy(NewMipData, CompressedImage.RawData.GetData(), CompressedImage.RawData.Num());
					NewMip->BulkData.Unlock();

					if (MipIndex == 0)
					{
						DerivedData->SizeX = CompressedImage.SizeX;
						DerivedData->SizeY = CompressedImage.SizeY;
						DerivedData->PixelFormat = (EPixelFormat)CompressedImage.PixelFormat;
						DerivedData->SetNumSlices(BuildSettingsPerLayer[0].bCubemap ? 6 : (BuildSettingsPerLayer[0].bVolume || BuildSettingsPerLayer[0].bTextureArray) ? CompressedImage.SizeZ : 1);
						DerivedData->SetIsCubemap(BuildSettingsPerLayer[0].bCubemap);
					}
					else
					{
						check(CompressedImage.PixelFormat == DerivedData->PixelFormat);
					}
				}

				DerivedData->SetOptData(OptData);

				BuildExporter.ExportTextureOutput(*DerivedData, BuildSettingsPerLayer[0]);
				
				// Store it in the cache.
				// @todo: This will remove the streaming bulk data, which we immediately reload below!
				// Should ideally avoid this redundant work, but it only happens when we actually have 
				// to build the texture, which should only ever be once.
				this->BytesCached = PutDerivedDataInCache(DerivedData, KeySuffix, Texture.GetPathName(), BuildSettingsPerLayer[0].bCubemap || (BuildSettingsPerLayer[0].bVolume && !GSupportsVolumeTextureStreaming) || (BuildSettingsPerLayer[0].bTextureArray && !GSupportsTexture2DArrayStreaming), bReplaceExistingDDC);
			}

			if (DerivedData->Mips.Num())
			{
				const bool bInlineMips = (CacheFlags & ETextureCacheFlags::InlineMips) != 0;
				bSucceeded = !bInlineMips || DerivedData->TryInlineMipData(BuildSettingsPerLayer[0].LODBiasWithCinematicMips, &Texture);
				if (!bSucceeded)
				{
					UE_LOG(LogTexture, Display, TEXT("Failed to put and then read back mipmap data from DDC for %s"), *Texture.GetPathName());
				}
			}
			else
			{
				UE_LOG(LogTexture, Warning, TEXT("Failed to build %s derived data for %s"), *BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *Texture.GetPathName());
			}
		}
	}
}

FTextureCacheDerivedDataWorker::FTextureCacheDerivedDataWorker(
	ITextureCompressorModule* InCompressor,
	FTexturePlatformData* InDerivedData,
	UTexture* InTexture,
	const FTextureBuildSettings* InSettingsPerLayer,
	uint32 InCacheFlags
	)
	: Compressor(InCompressor)
	, ImageWrapper(nullptr)
	, DerivedData(InDerivedData)
	, Texture(*InTexture)
	, CacheFlags(InCacheFlags)
	, RequiredMemoryEstimate(InTexture->GetBuildRequiredMemory())
	, bSucceeded(false)
{
	check(DerivedData);

	BuildSettingsPerLayer.SetNum(InTexture->Source.GetNumLayers());
	for (int32 LayerIndex = 0; LayerIndex < BuildSettingsPerLayer.Num(); ++LayerIndex)
	{
		BuildSettingsPerLayer[LayerIndex] = InSettingsPerLayer[LayerIndex];
	}

	// At this point, the texture *MUST* have a valid GUID.
	if (!Texture.Source.GetId().IsValid())
	{
		UE_LOG(LogTexture, Warning, TEXT("Building texture with an invalid GUID: %s"), *Texture.GetPathName());
		Texture.Source.ForceGenerateGuid();
	}
	check(Texture.Source.GetId().IsValid());

	// Dump any existing mips.
	DerivedData->Mips.Empty();
	if (DerivedData->VTData)
	{
		delete DerivedData->VTData;
		DerivedData->VTData = nullptr;
	}
	UTexture::GetPixelFormatEnum();
	GetTextureDerivedDataKeySuffix(Texture, InSettingsPerLayer, KeySuffix);
	BuildExporter.Init(KeySuffix);
		
	const bool bAllowAsyncBuild = (CacheFlags & ETextureCacheFlags::AllowAsyncBuild) != 0;
	const bool bAllowAsyncLoading = (CacheFlags & ETextureCacheFlags::AllowAsyncLoading) != 0;
	const bool bForVirtualTextureStreamingBuild = (CacheFlags & ETextureCacheFlags::ForVirtualTextureStreamingBuild) != 0;

	static const bool bBuildFunctionEnabled = FParse::Param(FCommandLine::Get(), TEXT("DDC2TextureBuilds"));
	if (bBuildFunctionEnabled && !bForVirtualTextureStreamingBuild && (BuildSettingsPerLayer.Num() == 1))
	{
		BuildFunctionName = GetTextureBuildFunctionName(BuildSettingsPerLayer[0]);
	}

	// FVirtualTextureDataBuilder always wants to load ImageWrapper module
	// This is not strictly necessary, used only for debug output, but seems simpler to just always load this here, doesn't seem like it should be too expensive
	if (bAllowAsyncLoading || bForVirtualTextureStreamingBuild)
	{
		ImageWrapper = &FModuleManager::LoadModuleChecked<IImageWrapperModule>( FName("ImageWrapper") );
	}

	TextureData.Init(Texture, BuildSettingsPerLayer.GetData(), bAllowAsyncLoading);
	if (Texture.CompositeTexture && Texture.CompositeTextureMode != CTM_Disabled)
	{
		bool bMatchingBlocks = Texture.CompositeTexture->Source.GetNumBlocks() == Texture.Source.GetNumBlocks();
		bool bMatchingAspectRatio = true;
		bool bOnlyPowerOfTwoSize = true;
		if (bMatchingBlocks)
		{
			for (int32 BlockIdx = 0; BlockIdx < Texture.Source.GetNumBlocks(); ++BlockIdx)
			{
				FTextureSourceBlock TextureBlock;
				Texture.Source.GetBlock(BlockIdx, TextureBlock);
				FTextureSourceBlock CompositeTextureBlock;
				Texture.CompositeTexture->Source.GetBlock(BlockIdx, CompositeTextureBlock);

				bMatchingBlocks = bMatchingBlocks && TextureBlock.BlockX == CompositeTextureBlock.BlockX && TextureBlock.BlockY == CompositeTextureBlock.BlockY;
				bMatchingAspectRatio = bMatchingAspectRatio && TextureBlock.SizeX * CompositeTextureBlock.SizeY == TextureBlock.SizeY * CompositeTextureBlock.SizeX;
				bOnlyPowerOfTwoSize = bOnlyPowerOfTwoSize && FMath::IsPowerOfTwo(TextureBlock.SizeX) && FMath::IsPowerOfTwo(TextureBlock.SizeY);
			}
		}

		if (!bMatchingBlocks)
		{
			UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Composite texture resolution/UDIMs do not match. Composite texture will be ignored"), *Texture.GetPathName());
		}
		else if (!bOnlyPowerOfTwoSize)
		{
			UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Some blocks (UDIMs) have a non power of two size. Composite texture will be ignored"), *Texture.GetPathName());
		}
		else if (!bMatchingAspectRatio)
		{
			UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Some blocks (UDIMs) have mismatched aspect ratio. Composite texture will be ignored"), *Texture.GetPathName());
		}

		if (bMatchingBlocks && bMatchingAspectRatio && bOnlyPowerOfTwoSize)
		{
			CompositeTextureData.Init(*Texture.CompositeTexture, BuildSettingsPerLayer.GetData(), bAllowAsyncLoading);
		}
	}
}

void FTextureCacheDerivedDataWorker::DoWork()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCacheDerivedDataWorker::DoWork);

	const bool bForceRebuild = (CacheFlags & ETextureCacheFlags::ForceRebuild) != 0;
	const bool bAllowAsyncBuild = (CacheFlags & ETextureCacheFlags::AllowAsyncBuild) != 0;
	const bool bAllowAsyncLoading = (CacheFlags & ETextureCacheFlags::AllowAsyncLoading) != 0;
	const bool bForVirtualTextureStreamingBuild = (CacheFlags & ETextureCacheFlags::ForVirtualTextureStreamingBuild) != 0;
	bool bInvalidVirtualTextureCompression = false;

	TArray<uint8> RawDerivedData;

	if (!bForceRebuild)
	{
		// First try to load a texture generated for the shipping build from the cache.
		// FTexturePlatformData::ShippingDerivedDataKey is set when we are running a build in the Editor.
		// This allows to preview how the texture will look in the final build and avoid rebuilding texture locally using fast cooking.
		if (!DerivedData->ShippingDerivedDataKey.IsEmpty() && DerivedData->ShippingDerivedDataKey != DerivedData->DerivedDataKey)
		{
			bLoadedFromDDC = GetDerivedDataCacheRef().GetSynchronous(*DerivedData->ShippingDerivedDataKey, RawDerivedData, Texture.GetPathName());
		}
		if (!bLoadedFromDDC)
		{
			bLoadedFromDDC = GetDerivedDataCacheRef().GetSynchronous(*DerivedData->DerivedDataKey, RawDerivedData, Texture.GetPathName());
		}
	}

	if (bLoadedFromDDC)
	{
		const bool bInlineMips = (CacheFlags & ETextureCacheFlags::InlineMips) != 0;
		const bool bForDDC = (CacheFlags & ETextureCacheFlags::ForDDCBuild) != 0;

		BytesCached = RawDerivedData.Num();
		FMemoryReader Ar(RawDerivedData, /*bIsPersistent=*/ true);
		DerivedData->Serialize(Ar, NULL);
		bSucceeded = true;
		// Load any streaming (not inline) mips that are necessary for our platform.
		if (bForDDC)
		{
			bSucceeded = DerivedData->TryLoadMips(0, nullptr, &Texture);

			if (!bSucceeded)
			{
				UE_LOG(LogTexture, Display, TEXT("Texture %s is missing mips. The texture will be rebuilt."), *Texture.GetFullName());
			}
		}
		else if (bInlineMips)
		{
			bSucceeded = DerivedData->TryInlineMipData(BuildSettingsPerLayer[0].LODBiasWithCinematicMips, &Texture);

			if (!bSucceeded)
			{
				UE_LOG(LogTexture, Display, TEXT("Texture %s is missing inline mips. The texture will be rebuilt."), *Texture.GetFullName());
			}
		}
		else
		{
			if (bForVirtualTextureStreamingBuild)
			{
				bSucceeded =	DerivedData->VTData != nullptr &&
								DerivedData->VTData->IsInitialized() &&
								DerivedData->AreDerivedVTChunksAvailable();

				if (!bSucceeded)
				{
					UE_LOG(LogTexture, Display, TEXT("Texture %s is missing VT Chunks. The texture will be rebuilt."), *Texture.GetFullName());
				}
			}
			else
			{
				bSucceeded = DerivedData->AreDerivedMipsAvailable();
				if (!bSucceeded)
				{
					UE_LOG(LogTexture, Display, TEXT("Texture %s is missing derived mips. The texture will be rebuilt."), *Texture.GetFullName());
				}

				if (bSucceeded && BuildSettingsPerLayer.Num() > 0)
				{
					// Code inspired by the texture compressor module as a hot fix for the bad data that might have been push into the ddc in 4.23 or 4.24 
					const bool bLongLatCubemap = DerivedData->IsCubemap() && DerivedData->GetNumSlices() == 1;
					int32 MaximumNumberOfMipMaps = TNumericLimits<int32>::Max();
					if (bLongLatCubemap)
					{
						MaximumNumberOfMipMaps = FMath::CeilLogTwo(FMath::Clamp<uint32>(uint32(1 << FMath::FloorLog2(DerivedData->SizeX / 2)), uint32(32), BuildSettingsPerLayer[0].MaxTextureResolution)) + 1;
					}
					else
					{
						MaximumNumberOfMipMaps = FMath::CeilLogTwo(FMath::Max3(DerivedData->SizeX, DerivedData->SizeY, BuildSettingsPerLayer[0].bVolume ? DerivedData->GetNumSlices() : 1)) + 1;
					}

					bSucceeded = DerivedData->Mips.Num() <= MaximumNumberOfMipMaps;

					if (!bSucceeded)
					{
						UE_LOG(LogTexture, Warning, TEXT("The data retrieved from the derived data cache for the texture %s was invalid. ")
							TEXT("The cached data has %d mips when a maximum of %d are expected. The texture will be rebuilt."),
							*Texture.GetFullName(), DerivedData->Mips.Num(), MaximumNumberOfMipMaps);
					}
				}
			}
		}

		if (bSucceeded && bForVirtualTextureStreamingBuild && CVarVTValidateCompressionOnLoad.GetValueOnAnyThread())
		{
			check(DerivedData->VTData);
			bSucceeded = DerivedData->VTData->ValidateData(Texture.GetPathName(), false);
			if (!bSucceeded)
			{
				UE_LOG(LogTexture, Display, TEXT("Texture %s has invalid cached VT data. The texture will be rebuilt."), *Texture.GetFullName());
				bInvalidVirtualTextureCompression = true;
			}
		}
		
		// Reset everything derived data so that we can do a clean load from the source data
		if (!bSucceeded)
		{
			DerivedData->Mips.Empty();
			if (DerivedData->VTData)
			{
				delete DerivedData->VTData;
				DerivedData->VTData = nullptr;
			}
			
			bLoadedFromDDC = false;
		}
	}
	
	if (!bSucceeded && bAllowAsyncBuild)
	{
		bool bHasTextureSourceMips = false;
		if (TextureData.IsValid() && Texture.Source.IsBulkDataLoaded())
		{
			TextureData.GetSourceMips(Texture.Source, ImageWrapper);
			BuildExporter.ExportTextureSourceBulkData(Texture.Source);
			bHasTextureSourceMips = true;
		}

		bool bHasCompositeTextureSourceMips = false;
		if (CompositeTextureData.IsValid() && Texture.CompositeTexture && Texture.CompositeTexture->Source.IsBulkDataLoaded())
		{
			CompositeTextureData.GetSourceMips(Texture.CompositeTexture->Source, ImageWrapper);
			BuildExporter.ExportCompositeTextureSourceBulkData(Texture.CompositeTexture->Source);
			bHasCompositeTextureSourceMips = true;
		}

		if (bAllowAsyncLoading && !bHasTextureSourceMips)
		{
			TextureData.GetAsyncSourceMips(ImageWrapper);
			BuildExporter.ExportTextureSourceBulkData(TextureData.AsyncSource);
			TextureData.AsyncSource.RemoveBulkData();
		}

		if (bAllowAsyncLoading && !bHasCompositeTextureSourceMips)
		{
			CompositeTextureData.GetAsyncSourceMips(ImageWrapper);
			if ((bool)Texture.CompositeTexture)
			{
				BuildExporter.ExportCompositeTextureSourceBulkData(CompositeTextureData.AsyncSource);
			}
			CompositeTextureData.AsyncSource.RemoveBulkData();
		}

		if (TextureData.Blocks.Num() && TextureData.Blocks[0].MipsPerLayer.Num() && TextureData.Blocks[0].MipsPerLayer[0].Num() && 
			(!CompositeTextureData.IsValid() || (CompositeTextureData.Blocks.Num() && CompositeTextureData.Blocks[0].MipsPerLayer.Num() && CompositeTextureData.Blocks[0].MipsPerLayer[0].Num())))
		{
			// Replace any existing DDC data, if corrupt compression was detected
			const bool bReplaceExistingDDC = bInvalidVirtualTextureCompression;
			BuildTexture(bReplaceExistingDDC);
			if (bInvalidVirtualTextureCompression && DerivedData->VTData)
			{
				// If we loaded data that turned out to be corrupt, flag it here so we can also recreate the VT data cached to local /DerivedDataCache/VT/ directory
				for (FVirtualTextureDataChunk& Chunk : DerivedData->VTData->Chunks)
				{
					Chunk.bCorruptDataLoadedFromDDC = true;
				}

			}

			bSucceeded = true;
		}
		else
		{
			bSucceeded = false;
		}
	}

	if (bSucceeded)
	{
		TextureData.ReleaseMemory();
		CompositeTextureData.ReleaseMemory();
	}
}

void FTextureCacheDerivedDataWorker::Finalize()
{
	// if we couldn't get from the DDC or didn't build synchronously, then we have to build now. 
	// This is a super edge case that should rarely happen.
	if (!bSucceeded)
	{
		TextureData.GetSourceMips(Texture.Source, ImageWrapper);
		if (Texture.CompositeTexture)
		{
			CompositeTextureData.GetSourceMips(Texture.CompositeTexture->Source, ImageWrapper);
		}
		BuildTexture();
	}
		
	if (bSucceeded && BuildSettingsPerLayer[0].bVirtualStreamable) // Texture.VirtualTextureStreaming is more a hint that might be overruled by the buildsettings
	{
		check((DerivedData->VTData != nullptr) == Texture.VirtualTextureStreaming); 
	}
}

#endif // WITH_EDITOR
