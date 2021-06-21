// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureDerivedDataBuildUtils.h"

#if WITH_EDITOR
#include "Engine/Texture.h"
#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatManagerModule.h"
#include "Interfaces/ITextureFormatModule.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "String/Find.h"
#include "TextureCompressorModule.h"
#include "TextureFormatManager.h"
#include "TextureResource.h"

const FGuid& GetTextureDerivedDataVersion();
void GetTextureDerivedMipKey(int32 MipIndex, const FTexture2DMipMap& Mip, const FString& KeySuffix, FString& OutKey);

template <typename ValueType>
static void WriteCbField(FCbWriter& Writer, FAnsiStringView Name, const ValueType& Value)
{
	Writer << Name << Value;
}

static void WriteCbField(FCbWriter& Writer, FAnsiStringView Name, const FColor& Value)
{
	Writer.BeginArray(Name);
	Writer.AddInteger(Value.A);
	Writer.AddInteger(Value.R);
	Writer.AddInteger(Value.G);
	Writer.AddInteger(Value.B);
	Writer.EndArray();
}

static void WriteCbField(FCbWriter& Writer, FAnsiStringView Name, const FVector4& Value)
{
	Writer.BeginArray(Name);
	Writer.AddFloat(Value.X);
	Writer.AddFloat(Value.Y);
	Writer.AddFloat(Value.Z);
	Writer.AddFloat(Value.W);
	Writer.EndArray();
}

static void WriteCbField(FCbWriter& Writer, FAnsiStringView Name, const FIntPoint& Value)
{
	Writer.BeginArray(Name);
	Writer.AddInteger(Value.X);
	Writer.AddInteger(Value.Y);
	Writer.EndArray();
}

template <typename ValueType>
static void WriteCbFieldWithDefault(FCbWriter& Writer, FAnsiStringView Name, ValueType Value, ValueType Default)
{
	if (Value != Default)
	{
		WriteCbField(Writer, Name, Forward<ValueType>(Value));
	}
}

static void WriteBuildSettings(FCbWriter& Writer, const FTextureBuildSettings& BuildSettings, const ITextureFormat* TextureFormat)
{
	FTextureBuildSettings DefaultSettings;

	Writer.BeginObject();

	if (BuildSettings.FormatConfigOverride)
	{
		Writer.AddObject("FormatConfigOverride", BuildSettings.FormatConfigOverride);
	}
	else if (FCbObject TextureFormatConfig = TextureFormat->ExportGlobalFormatConfig(BuildSettings))
	{
		Writer.AddObject("FormatConfigOverride", TextureFormatConfig);
	}

	if (BuildSettings.ColorAdjustment.AdjustBrightness != DefaultSettings.ColorAdjustment.AdjustBrightness ||
		BuildSettings.ColorAdjustment.AdjustBrightnessCurve != DefaultSettings.ColorAdjustment.AdjustBrightnessCurve ||
		BuildSettings.ColorAdjustment.AdjustSaturation != DefaultSettings.ColorAdjustment.AdjustSaturation ||
		BuildSettings.ColorAdjustment.AdjustVibrance != DefaultSettings.ColorAdjustment.AdjustVibrance ||
		BuildSettings.ColorAdjustment.AdjustRGBCurve != DefaultSettings.ColorAdjustment.AdjustRGBCurve ||
		BuildSettings.ColorAdjustment.AdjustHue != DefaultSettings.ColorAdjustment.AdjustHue ||
		BuildSettings.ColorAdjustment.AdjustMinAlpha != DefaultSettings.ColorAdjustment.AdjustMinAlpha ||
		BuildSettings.ColorAdjustment.AdjustMaxAlpha != DefaultSettings.ColorAdjustment.AdjustMaxAlpha)
	{
		Writer.BeginObject("ColorAdjustment");
		WriteCbFieldWithDefault(Writer, "AdjustBrightness", BuildSettings.ColorAdjustment.AdjustBrightness, DefaultSettings.ColorAdjustment.AdjustBrightness);
		WriteCbFieldWithDefault(Writer, "AdjustBrightnessCurve", BuildSettings.ColorAdjustment.AdjustBrightnessCurve, DefaultSettings.ColorAdjustment.AdjustBrightnessCurve);
		WriteCbFieldWithDefault(Writer, "AdjustSaturation", BuildSettings.ColorAdjustment.AdjustSaturation, DefaultSettings.ColorAdjustment.AdjustSaturation);
		WriteCbFieldWithDefault(Writer, "AdjustVibrance", BuildSettings.ColorAdjustment.AdjustVibrance, DefaultSettings.ColorAdjustment.AdjustVibrance);
		WriteCbFieldWithDefault(Writer, "AdjustRGBCurve", BuildSettings.ColorAdjustment.AdjustRGBCurve, DefaultSettings.ColorAdjustment.AdjustRGBCurve);
		WriteCbFieldWithDefault(Writer, "AdjustHue", BuildSettings.ColorAdjustment.AdjustHue, DefaultSettings.ColorAdjustment.AdjustHue);
		WriteCbFieldWithDefault(Writer, "AdjustMinAlpha", BuildSettings.ColorAdjustment.AdjustMinAlpha, DefaultSettings.ColorAdjustment.AdjustMinAlpha);
		WriteCbFieldWithDefault(Writer, "AdjustMaxAlpha", BuildSettings.ColorAdjustment.AdjustMaxAlpha, DefaultSettings.ColorAdjustment.AdjustMaxAlpha);
		Writer.EndObject();
	}

	WriteCbFieldWithDefault(Writer, "AlphaCoverageThresholds", BuildSettings.AlphaCoverageThresholds, DefaultSettings.AlphaCoverageThresholds);
	WriteCbFieldWithDefault(Writer, "MipSharpening", BuildSettings.MipSharpening, DefaultSettings.MipSharpening);
	WriteCbFieldWithDefault(Writer, "DiffuseConvolveMipLevel", BuildSettings.DiffuseConvolveMipLevel, DefaultSettings.DiffuseConvolveMipLevel);
	WriteCbFieldWithDefault(Writer, "SharpenMipKernelSize", BuildSettings.SharpenMipKernelSize, DefaultSettings.SharpenMipKernelSize);
	WriteCbFieldWithDefault(Writer, "MaxTextureResolution", BuildSettings.MaxTextureResolution, DefaultSettings.MaxTextureResolution);
	WriteCbFieldWithDefault(Writer, "TextureFormatName", WriteToString<64>(BuildSettings.TextureFormatName).ToView(), TEXT(""_SV));
	WriteCbFieldWithDefault(Writer, "bHDRSource", BuildSettings.bHDRSource, DefaultSettings.bHDRSource);
	WriteCbFieldWithDefault(Writer, "MipGenSettings", BuildSettings.MipGenSettings, DefaultSettings.MipGenSettings);
	WriteCbFieldWithDefault(Writer, "bCubemap", BuildSettings.bCubemap, DefaultSettings.bCubemap);
	WriteCbFieldWithDefault(Writer, "bTextureArray", BuildSettings.bTextureArray, DefaultSettings.bTextureArray);
	WriteCbFieldWithDefault(Writer, "bVolume", BuildSettings.bVolume, DefaultSettings.bVolume);
	WriteCbFieldWithDefault(Writer, "bLongLatSource", BuildSettings.bLongLatSource, DefaultSettings.bLongLatSource);
	WriteCbFieldWithDefault(Writer, "bSRGB", BuildSettings.bSRGB, DefaultSettings.bSRGB);
	WriteCbFieldWithDefault(Writer, "bUseLegacyGamma", BuildSettings.bUseLegacyGamma, DefaultSettings.bUseLegacyGamma);
	WriteCbFieldWithDefault(Writer, "bPreserveBorder", BuildSettings.bPreserveBorder, DefaultSettings.bPreserveBorder);
	WriteCbFieldWithDefault(Writer, "bForceNoAlphaChannel", BuildSettings.bForceNoAlphaChannel, DefaultSettings.bForceNoAlphaChannel);
	WriteCbFieldWithDefault(Writer, "bForceAlphaChannel", BuildSettings.bForceAlphaChannel, DefaultSettings.bForceAlphaChannel);
	WriteCbFieldWithDefault(Writer, "bDitherMipMapAlpha", BuildSettings.bDitherMipMapAlpha, DefaultSettings.bDitherMipMapAlpha);
	WriteCbFieldWithDefault(Writer, "bComputeBokehAlpha", BuildSettings.bComputeBokehAlpha, DefaultSettings.bComputeBokehAlpha);
	WriteCbFieldWithDefault(Writer, "bReplicateRed", BuildSettings.bReplicateRed, DefaultSettings.bReplicateRed);
	WriteCbFieldWithDefault(Writer, "bReplicateAlpha", BuildSettings.bReplicateAlpha, DefaultSettings.bReplicateAlpha);
	WriteCbFieldWithDefault(Writer, "bDownsampleWithAverage", BuildSettings.bDownsampleWithAverage, DefaultSettings.bDownsampleWithAverage);
	WriteCbFieldWithDefault(Writer, "bSharpenWithoutColorShift", BuildSettings.bSharpenWithoutColorShift, DefaultSettings.bSharpenWithoutColorShift);
	WriteCbFieldWithDefault(Writer, "bBorderColorBlack", BuildSettings.bBorderColorBlack, DefaultSettings.bBorderColorBlack);
	WriteCbFieldWithDefault(Writer, "bFlipGreenChannel", BuildSettings.bFlipGreenChannel, DefaultSettings.bFlipGreenChannel);
	WriteCbFieldWithDefault(Writer, "bApplyYCoCgBlockScale", BuildSettings.bApplyYCoCgBlockScale, DefaultSettings.bApplyYCoCgBlockScale);
	WriteCbFieldWithDefault(Writer, "bApplyKernelToTopMip", BuildSettings.bApplyKernelToTopMip, DefaultSettings.bApplyKernelToTopMip);
	WriteCbFieldWithDefault(Writer, "bRenormalizeTopMip", BuildSettings.bRenormalizeTopMip, DefaultSettings.bRenormalizeTopMip);
	WriteCbFieldWithDefault(Writer, "CompositeTextureMode", BuildSettings.CompositeTextureMode, DefaultSettings.CompositeTextureMode);
	WriteCbFieldWithDefault(Writer, "CompositePower", BuildSettings.CompositePower, DefaultSettings.CompositePower);
	WriteCbFieldWithDefault(Writer, "LODBias", BuildSettings.LODBias, DefaultSettings.LODBias);
	WriteCbFieldWithDefault(Writer, "LODBiasWithCinematicMips", BuildSettings.LODBiasWithCinematicMips, DefaultSettings.LODBiasWithCinematicMips);
	WriteCbFieldWithDefault(Writer, "TopMipSize", BuildSettings.TopMipSize, DefaultSettings.TopMipSize);
	WriteCbFieldWithDefault(Writer, "VolumeSizeZ", BuildSettings.VolumeSizeZ, DefaultSettings.VolumeSizeZ);
	WriteCbFieldWithDefault(Writer, "ArraySlices", BuildSettings.ArraySlices, DefaultSettings.ArraySlices);
	WriteCbFieldWithDefault(Writer, "bStreamable", BuildSettings.bStreamable, DefaultSettings.bStreamable);
	WriteCbFieldWithDefault(Writer, "bVirtualStreamable", BuildSettings.bVirtualStreamable, DefaultSettings.bVirtualStreamable);
	WriteCbFieldWithDefault(Writer, "bChromaKeyTexture", BuildSettings.bChromaKeyTexture, DefaultSettings.bChromaKeyTexture);
	WriteCbFieldWithDefault(Writer, "PowerOfTwoMode", BuildSettings.PowerOfTwoMode, DefaultSettings.PowerOfTwoMode);
	WriteCbFieldWithDefault(Writer, "PaddingColor", BuildSettings.PaddingColor, DefaultSettings.PaddingColor);
	WriteCbFieldWithDefault(Writer, "ChromaKeyColor", BuildSettings.ChromaKeyColor, DefaultSettings.ChromaKeyColor);
	WriteCbFieldWithDefault(Writer, "ChromaKeyThreshold", BuildSettings.ChromaKeyThreshold, DefaultSettings.ChromaKeyThreshold);
	WriteCbFieldWithDefault(Writer, "CompressionQuality", BuildSettings.CompressionQuality, DefaultSettings.CompressionQuality);
	WriteCbFieldWithDefault(Writer, "LossyCompressionAmount", BuildSettings.LossyCompressionAmount, DefaultSettings.LossyCompressionAmount);
	WriteCbFieldWithDefault(Writer, "Downscale", BuildSettings.Downscale, DefaultSettings.Downscale);
	WriteCbFieldWithDefault(Writer, "DownscaleOptions", BuildSettings.DownscaleOptions, DefaultSettings.DownscaleOptions);
	WriteCbFieldWithDefault(Writer, "VirtualAddressingModeX", BuildSettings.VirtualAddressingModeX, DefaultSettings.VirtualAddressingModeX);
	WriteCbFieldWithDefault(Writer, "VirtualAddressingModeY", BuildSettings.VirtualAddressingModeY, DefaultSettings.VirtualAddressingModeY);
	WriteCbFieldWithDefault(Writer, "VirtualTextureTileSize", BuildSettings.VirtualTextureTileSize, DefaultSettings.VirtualTextureTileSize);
	WriteCbFieldWithDefault(Writer, "VirtualTextureBorderSize", BuildSettings.VirtualTextureBorderSize, DefaultSettings.VirtualTextureBorderSize);
	WriteCbFieldWithDefault(Writer, "bVirtualTextureEnableCompressZlib", BuildSettings.bVirtualTextureEnableCompressZlib, DefaultSettings.bVirtualTextureEnableCompressZlib);
	WriteCbFieldWithDefault(Writer, "bVirtualTextureEnableCompressCrunch", BuildSettings.bVirtualTextureEnableCompressCrunch, DefaultSettings.bVirtualTextureEnableCompressCrunch);
	WriteCbFieldWithDefault(Writer, "bHasEditorOnlyData", BuildSettings.bHasEditorOnlyData, DefaultSettings.bHasEditorOnlyData);

	Writer.EndObject();
}

static void WriteOutputSettings(FCbWriter& Writer, int32 NumInlineMips, const FString& KeySuffix)
{
	Writer.BeginObject();

	Writer.AddInteger("NumInlineMips", NumInlineMips);
	
	FString MipDerivedDataKey;
	FTexture2DMipMap DummyMip;
	DummyMip.SizeX = 0;
	DummyMip.SizeY = 0;
	GetTextureDerivedMipKey(0, DummyMip, KeySuffix, MipDerivedDataKey);
	int32 PrefixEndIndex = MipDerivedDataKey.Find(TEXT("_MIP0_"), ESearchCase::CaseSensitive);
	check(PrefixEndIndex != -1);
	MipDerivedDataKey.LeftInline(PrefixEndIndex);
	check(!MipDerivedDataKey.IsEmpty());
	Writer.AddString("MipKeyPrefix",*MipDerivedDataKey);

	Writer.EndObject();
}

static void WriteSource(FCbWriter& Writer, const UTexture& Texture, int32 LayerIndex)
{
	const FTextureSource& Source = Texture.Source;

	FTextureFormatSettings TextureFormatSettings;
	Texture.GetLayerFormatSettings(LayerIndex, TextureFormatSettings);
	EGammaSpace GammaSpace = TextureFormatSettings.SRGB ? (Texture.bUseLegacyGamma ? EGammaSpace::Pow22 : EGammaSpace::sRGB) : EGammaSpace::Linear;

	Writer.BeginObject();

	Writer.AddInteger("CompressionFormat", Source.GetSourceCompression());
	Writer.AddInteger("SourceFormat", Source.GetFormat(LayerIndex));
	Writer.AddInteger("GammaSpace", static_cast<uint8>(GammaSpace));
	Writer.AddInteger("NumSlices", Source.GetNumSlices());
	Writer.AddInteger("SizeX", Source.GetSizeX());
	Writer.AddInteger("SizeY", Source.GetSizeY());
	Writer.BeginArray("Mips");
	int64 Offset = 0;
	for (int32 MipIndex = 0, MipCount = Source.GetNumMips(); MipIndex < MipCount; ++MipIndex)
	{
		Writer.BeginObject();
		Writer.AddInteger("Offset", Offset);
		const int64 MipSize = Source.CalcMipSize(MipIndex);
		Writer.AddInteger("Size", MipSize);
		Offset += MipSize;
		Writer.EndObject();
	}
	Writer.EndArray();

	Writer.EndObject();
}

FString GetTextureBuildFunctionName(const FTextureBuildSettings& BuildSettings)
{
	FName TextureFormatModuleName;

	if (ITextureFormatManagerModule* TFM = GetTextureFormatManager())
	{
		ITextureFormatModule* TextureFormatModule = nullptr;
		if (!TFM->FindTextureFormatAndModule(BuildSettings.TextureFormatName, TextureFormatModuleName, TextureFormatModule))
		{
			return FString();
		}
	}

	// Texture format modules are inconsistent in their naming, e.g., TextureFormatUncompressed, <Platform>TextureFormat.
	// Attempt to unify the naming of build functions as <Format>Texture.
	TStringBuilder<64> FunctionName;
	FunctionName << TextureFormatModuleName << TEXT("Texture"_SV);
	if (int32 Index = UE::String::FindFirst(FunctionName, TEXT("TextureFormat"_SV)); Index != INDEX_NONE)
	{
		FunctionName.RemoveAt(Index, TEXT("TextureFormat"_SV).Len());
	}
	return FString(FunctionName);
}

FCbObject SaveTextureBuildSettings(const FString& KeySuffix, const UTexture& Texture, const FTextureBuildSettings& BuildSettings, int32 LayerIndex, int32 NumInlineMips)
{
	const ITextureFormat* TextureFormat = nullptr;
	if (ITextureFormatManagerModule* TFM = GetTextureFormatManager())
	{
		FName TextureFormatModuleName;
		ITextureFormatModule* TextureFormatModule = nullptr;
		TextureFormat = TFM->FindTextureFormatAndModule(BuildSettings.TextureFormatName, TextureFormatModuleName, TextureFormatModule);
	}
	if (TextureFormat == nullptr)
	{
		return FCbObject();
	}

	FCbWriter Writer;
	Writer.BeginObject();

	Writer.AddUuid("BuildVersion", GetTextureDerivedDataVersion());

	if (uint16 TextureFormatVersion = TextureFormat->GetVersion(BuildSettings.TextureFormatName, &BuildSettings))
	{
		Writer.AddInteger("FormatVersion", TextureFormatVersion);
	}

	Writer.SetName("Build");
	WriteBuildSettings(Writer, BuildSettings, TextureFormat);

	Writer.SetName("Output");
	WriteOutputSettings(Writer, NumInlineMips, KeySuffix);

	Writer.SetName("Source");
	WriteSource(Writer, Texture, LayerIndex);

	if (Texture.CompositeTexture)
	{
		Writer.SetName("CompositeSource");
		WriteSource(Writer, *Texture.CompositeTexture, LayerIndex);
	}

	Writer.EndObject();
	return Writer.Save().AsObject();
}

#endif // WITH_EDITOR
